/**
 * STUN Client for NAT Hole Punching
 * Simple STUN RFC 5389 implementation
 */

#include "ipfs/stun.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #define closesocket close
#endif

/* STUN Message Types */
#define STUN_BINDING_REQUEST    0x0001
#define STUN_BINDING_RESPONSE   0x0101

/* STUN Attributes */
#define STUN_ATTR_MAPPED_ADDRESS    0x0001
#define STUN_ATTR_XOR_MAPPED_ADDRESS 0x0020

/* STUN Magic Cookie */
#define STUN_MAGIC_COOKIE       0x2112A442

/* STUN Transaction ID Length */
#define STUN_TRANSACTION_ID_LEN 12

/* STUN Header Length */
#define STUN_HEADER_LEN         20

/* Default STUN Servers */
static const char* DEFAULT_STUN_SERVERS[] = {
    "stun.l.google.com:19302",
    "stun1.l.google.com:19302",
    "stun.stunprotocol.org:3478",
    NULL
};

/* Generate random transaction ID */
static void generate_transaction_id(uint8_t* id) {
    for (int i = 0; i < STUN_TRANSACTION_ID_LEN; i++) {
        id[i] = rand() % 256;
    }
}

/* Create STUN Binding Request */
static int create_binding_request(uint8_t* buffer, size_t* len) {
    /* STUN Header */
    uint16_t msg_type = htons(STUN_BINDING_REQUEST);
    uint16_t msg_len = htons(0);  /* No attributes in request */
    uint32_t magic = htonl(STUN_MAGIC_COOKIE);
    uint8_t transaction_id[STUN_TRANSACTION_ID_LEN];
    
    generate_transaction_id(transaction_id);
    
    memcpy(buffer, &msg_type, 2);
    memcpy(buffer + 2, &msg_len, 2);
    memcpy(buffer + 4, &magic, 4);
    memcpy(buffer + 8, transaction_id, STUN_TRANSACTION_ID_LEN);
    
    *len = STUN_HEADER_LEN;
    return 0;
}

/* Parse STUN Binding Response */
static int parse_binding_response(const uint8_t* buffer, size_t len,
                                   char* ip_out, size_t ip_len, uint16_t* port_out) {
    if (len < STUN_HEADER_LEN) {
        return -1;
    }
    
    /* Check message type */
    uint16_t msg_type;
    memcpy(&msg_type, buffer, 2);
    msg_type = ntohs(msg_type);
    
    if (msg_type != STUN_BINDING_RESPONSE) {
        return -1;
    }
    
    /* Get message length */
    uint16_t attrs_len;
    memcpy(&attrs_len, buffer + 2, 2);
    attrs_len = ntohs(attrs_len);
    
    /* Parse attributes */
    size_t offset = STUN_HEADER_LEN;
    while (offset < STUN_HEADER_LEN + attrs_len) {
        uint16_t attr_type, attr_len;
        memcpy(&attr_type, buffer + offset, 2);
        memcpy(&attr_len, buffer + offset + 2, 2);
        attr_type = ntohs(attr_type);
        attr_len = ntohs(attr_len);
        
        /* XOR-MAPPED-ADDRESS or MAPPED-ADDRESS */
        if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS || 
            attr_type == STUN_ATTR_MAPPED_ADDRESS) {
            
            /* Skip reserved byte */
            uint8_t family = buffer[offset + 4];
            uint16_t port;
            memcpy(&port, buffer + offset + 5, 2);
            port = ntohs(port);
            
            /* XOR with magic cookie if XOR-MAPPED-ADDRESS */
            if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS) {
                port ^= (STUN_MAGIC_COOKIE >> 16);
            }
            *port_out = port;
            
            /* IPv4 */
            if (family == 0x01) {
                uint8_t ip[4];
                memcpy(ip, buffer + offset + 7, 4);
                
                if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS) {
                    uint8_t magic_bytes[4];
                    uint32_t magic = htonl(STUN_MAGIC_COOKIE);
                    memcpy(magic_bytes, &magic, 4);
                    for (int i = 0; i < 4; i++) {
                        ip[i] ^= magic_bytes[i];
                    }
                }
                
                snprintf(ip_out, ip_len, "%d.%d.%d.%d", 
                         ip[0], ip[1], ip[2], ip[3]);
                return 0;
            }
        }
        
        offset += 4 + attr_len;
        /* Align to 4-byte boundary */
        if (attr_len % 4 != 0) {
            offset += 4 - (attr_len % 4);
        }
    }
    
    return -1;
}

/* Discover public IP and port via STUN */
int stun_discover_public_ip(char* public_ip, size_t ip_len, uint16_t* public_port,
                             const char* stun_server) {
    int sock = -1;
    int result = -1;
    
    /* Parse STUN server */
    char server_addr[256];
    uint16_t server_port = 3478;
    
    const char* colon = strchr(stun_server, ':');
    if (colon) {
        size_t addr_len = colon - stun_server;
        if (addr_len >= sizeof(server_addr)) {
            addr_len = sizeof(server_addr) - 1;
        }
        memcpy(server_addr, stun_server, addr_len);
        server_addr[addr_len] = '\0';
        server_port = (uint16_t)atoi(colon + 1);
    } else {
        strncpy(server_addr, stun_server, sizeof(server_addr) - 1);
        server_addr[sizeof(server_addr) - 1] = '\0';
    }
    
    /* Create UDP socket */
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        fprintf(stderr, "Failed to create STUN socket\n");
        return -1;
    }
    
    /* Set timeout */
#ifdef _WIN32
    DWORD timeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    
    /* Resolve STUN server address */
    struct sockaddr_in stun_addr;
    memset(&stun_addr, 0, sizeof(stun_addr));
    stun_addr.sin_family = AF_INET;
    stun_addr.sin_port = htons(server_port);
    
    if (inet_pton(AF_INET, server_addr, &stun_addr.sin_addr) <= 0) {
        /* Try DNS resolution */
        struct hostent* he = gethostbyname(server_addr);
        if (!he) {
            fprintf(stderr, "Failed to resolve STUN server: %s\n", stun_server);
            closesocket(sock);
            return -1;
        }
        memcpy(&stun_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }
    
    /* Create and send binding request */
    uint8_t request[STUN_HEADER_LEN];
    size_t request_len;
    create_binding_request(request, &request_len);
    
    if (sendto(sock, (char*)request, request_len, 0,
               (struct sockaddr*)&stun_addr, sizeof(stun_addr)) < 0) {
        fprintf(stderr, "Failed to send STUN request\n");
        closesocket(sock);
        return -1;
    }
    
    /* Receive response */
    uint8_t response[512];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    ssize_t recv_len = recvfrom(sock, (char*)response, sizeof(response), 0,
                                 (struct sockaddr*)&from_addr, &from_len);
    
    if (recv_len < 0) {
        fprintf(stderr, "STUN request timed out\n");
        closesocket(sock);
        return -1;
    }
    
    /* Parse response */
    if (parse_binding_response(response, recv_len, public_ip, ip_len, public_port) == 0) {
        result = 0;
    } else {
        fprintf(stderr, "Failed to parse STUN response\n");
    }
    
    closesocket(sock);
    return result;
}

/* Try multiple STUN servers */
int stun_get_public_ip(char* public_ip, size_t ip_len, uint16_t* public_port) {
    for (int i = 0; DEFAULT_STUN_SERVERS[i] != NULL; i++) {
        fprintf(stderr, "Trying STUN server: %s\n", DEFAULT_STUN_SERVERS[i]);
        
        if (stun_discover_public_ip(public_ip, ip_len, public_port,
                                     DEFAULT_STUN_SERVERS[i]) == 0) {
            fprintf(stderr, "Discovered public IP: %s:%d\n", public_ip, *public_port);
            return 0;
        }
    }
    
    fprintf(stderr, "All STUN servers failed\n");
    return -1;
}

/* Get local IP address */
int stun_get_local_ip(char* local_ip, size_t ip_len) {
#ifdef _WIN32
    /* Windows: Use GetAdaptersAddresses */
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct hostent* he = gethostbyname(hostname);
        if (he && he->h_addr_list[0]) {
            struct in_addr* addr = (struct in_addr*)he->h_addr_list[0];
            strncpy(local_ip, inet_ntoa(*addr), ip_len - 1);
            local_ip[ip_len - 1] = '\0';
            return 0;
        }
    }
#else
    /* Linux: Use getifaddrs */
    #include <ifaddrs.h>
    struct ifaddrs* ifaddr;
    
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in* addr = (struct sockaddr_in*)ifa->ifa_addr;
                char* ip = inet_ntoa(addr->sin_addr);
                
                /* Skip loopback */
                if (strncmp(ip, "127.", 4) != 0) {
                    strncpy(local_ip, ip, ip_len - 1);
                    local_ip[ip_len - 1] = '\0';
                    freeifaddrs(ifaddr);
                    return 0;
                }
            }
        }
        freeifaddrs(ifaddr);
    }
#endif
    
    /* Fallback */
    strncpy(local_ip, "127.0.0.1", ip_len - 1);
    local_ip[ip_len - 1] = '\0';
    return 0;
}
