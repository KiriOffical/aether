/**
 * Core Protocol Implementation
 */

#include "protocol.h"
#include "crypto.h"
#include "handshake.h"
#include "framing.h"
#include "message.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

#define LOG_ERROR 0
#define LOG_WARN  1
#define LOG_INFO  2
#define LOG_DEBUG 3

void protocol_log(aether_node_t* node, int level, const char* fmt, ...) {
    if (level > node->log_level) return;
    
    const char* levels[] = {"ERROR", "WARN", "INFO", "DEBUG"};
    printf("[%s] ", levels[level]);
    
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

int protocol_init(aether_node_t* node, const aether_config_t* config) {
    memset(node, 0, sizeof(aether_node_t));
    memcpy(&node->config, config, sizeof(aether_config_t));
    node->log_level = config->log_level;
    
    /* Initialize crypto */
    crypto_init();
    
    /* Load or generate identity */
    if (config->identity_path[0] != '\0') {
        if (crypto_keypair_load((crypto_keypair_t*)node->secret_key, config->identity_path) != 0) {
            crypto_keypair_t kp;
            crypto_keypair_generate(&kp);
            memcpy(node->secret_key, kp.secret_key, 64);
            crypto_keypair_save(&kp, config->identity_path);
            crypto_node_id(node->node_id, kp.public_key);
        }
    } else {
        crypto_keypair_t kp;
        crypto_keypair_generate(&kp);
        memcpy(node->secret_key, kp.secret_key, 64);
        crypto_node_id(node->node_id, kp.public_key);
    }
    
    /* Initialize DHT */
    dht_endpoint_t endpoint;
    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.port = config->listen_port;
    if (dht_init(&node->dht, (const dht_node_id_t*)&node->node_id, &endpoint) != 0) {
        return -1;
    }
    
    /* Initialize peer manager */
    if (peer_manager_init(&node->peer_manager, node->node_id, config->max_connections) != 0) {
        dht_free(&node->dht);
        return -1;
    }
    
    protocol_log(node, LOG_INFO, "Node ID: %02x%02x...%02x%02x",
                 node->node_id[0], node->node_id[1],
                 node->node_id[30], node->node_id[31]);
    
    return 0;
}

void protocol_free(aether_node_t* node) {
    dht_free(&node->dht);
    peer_manager_free(&node->peer_manager);
}

#ifdef _WIN32
static int do_create_socket(uint16_t port, SOCKET* out_sock) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return -1;
    
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return -1;
    }
    
    if (listen(sock, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(sock);
        return -1;
    }
    
    *out_sock = sock;
    return 0;
}
#else
static int do_create_socket(uint16_t port, int* out_sock) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    
    if (listen(sock, SOMAXCONN) < 0) {
        close(sock);
        return -1;
    }
    
    *out_sock = sock;
    return 0;
}
#endif

int protocol_start(aether_node_t* node) {
#ifdef _WIN32
    SOCKET sock;
    if (do_create_socket(node->config.listen_port, &sock) != 0) return -1;
    node->socket = (void*)(intptr_t)sock;
#else
    int sock;
    if (do_create_socket(node->config.listen_port, &sock) != 0) return -1;
    node->socket = (void*)(intptr_t)sock;
#endif
    
    node->running = 1;
    protocol_log(node, LOG_INFO, "Listening on port %u", node->config.listen_port);
    return 0;
}

int protocol_stop(aether_node_t* node) {
    node->running = 0;
    
#ifdef _WIN32
    if (node->socket) {
        closesocket((SOCKET)(intptr_t)node->socket);
        WSACleanup();
    }
#else
    if (node->socket) {
        close((int)(intptr_t)node->socket);
    }
#endif
    
    node->socket = NULL;
    protocol_log(node, LOG_INFO, "Stopped");
    return 0;
}

int protocol_run(aether_node_t* node) {
#ifdef _WIN32
    SOCKET listen_sock = (SOCKET)(intptr_t)node->socket;
#else
    int listen_sock = (int)(intptr_t)node->socket;
#endif
    
    uint64_t last_ping = 0;
    uint64_t last_cleanup = 0;
    
    while (node->running) {
#ifdef _WIN32
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listen_sock, &fds);
        
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int ret = select(0, &fds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(listen_sock, &fds)) {
            struct sockaddr_in client_addr;
            int client_len = sizeof(client_addr);
            SOCKET client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
            if (client_sock != INVALID_SOCKET) {
                protocol_log(node, LOG_DEBUG, "New connection");
                closesocket(client_sock);
            }
        }
#else
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listen_sock, &fds);
        
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int ret = select(listen_sock + 1, &fds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(listen_sock, &fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
            if (client_sock >= 0) {
                protocol_log(node, LOG_DEBUG, "New connection");
                close(client_sock);
            }
        }
#endif
        
        uint64_t now = (uint64_t)time(NULL);
        
        if (now - last_ping >= PROTOCOL_PING_INTERVAL_SECS) {
            last_ping = now;
        }
        
        if (now - last_cleanup >= PROTOCOL_CLEANUP_INTERVAL_SECS) {
            peer_manager_evict_stale(&node->peer_manager);
            dht_cleanup(&node->dht);
            last_cleanup = now;
        }
    }
    
    return 0;
}

int protocol_send(aether_node_t* node, const uint8_t* target_id, const uint8_t* data, size_t len) {
    (void)node; (void)target_id; (void)data; (void)len;
    return 0;
}

int protocol_broadcast(aether_node_t* node, const uint8_t* data, size_t len) {
    (void)node; (void)data; (void)len;
    return 0;
}
