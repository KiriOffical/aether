/**
 * IPFS-like P2P Node - Windows Safe Version
 * Modified to reduce false positives from Windows Defender
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <windows.h>
#include "ipfs/node.h"

/* Windows version info resource */
#define APP_NAME "A.E.T.H.E.R. IPFS Node"
#define APP_VERSION "1.0.0"

static void print_usage(const wchar_t* program) {
    wprintf(L"%s\n", APP_NAME);
    wprintf(L"Version: %s\n\n", APP_VERSION);
    wprintf(L"Usage: %s <command> [options]\n\n", program);
    wprintf(L"Commands:\n");
    wprintf(L"  daemon              Start the IPFS node daemon\n");
    wprintf(L"  add <file>          Add a file to the node\n");
    wprintf(L"  get <cid> [output]  Get a file by CID\n");
    wprintf(L"  pin <cid>           Pin a CID locally\n");
    wprintf(L"  unpin <cid>         Unpin a CID\n");
    wprintf(L"  status              Show node status\n");
    wprintf(L"  help                Show this help message\n");
    wprintf(L"\nDaemon Options:\n");
    wprintf(L"  --port <num>        HTTP port (default: 8080)\n");
    wprintf(L"  --udp-port <num>    UDP port (default: 4001)\n");
    wprintf(L"  --tcp-port <num>    TCP port (default: 4002)\n");
    wprintf(L"  --data-dir <path>   Data directory (default: %%USERPROFILE%%\\.my_ipfs)\n");
    wprintf(L"\nExamples:\n");
    wprintf(L"  %s daemon                    Start daemon\n", program);
    wprintf(L"  %s add myfile.txt            Add file, prints CID\n", program);
    wprintf(L"  %s get <cid> output.txt      Get file by CID\n", program);
    wprintf(L"\n");
}

static void print_cid(const uint8_t* cid) {
    for (int i = 0; i < IPFS_CID_SIZE; i++) {
        printf("%02x", cid[i]);
    }
    printf("\n");
}

/* Global node for signal handling */
static ipfs_node_t* g_node = NULL;

static void signal_handler(int sig) {
    printf("\n[IPFS Node] Received signal %d, shutting down...\n", sig);
    if (g_node) {
        ipfs_node_request_stop(g_node);
    }
}

int wmain(int argc, wchar_t* argv[]) {
    /* Convert wide args to narrow */
    char** args = (char**)malloc(argc * sizeof(char*));
    if (!args) return 1;
    
    for (int i = 0; i < argc; i++) {
        size_t len = wcslen(argv[i]) + 1;
        args[i] = (char*)malloc(len * 4);
        if (args[i]) {
            wcstombs(args[i], argv[i], len * 4);
        }
    }
    
    if (argc < 2) {
        print_usage(argv[0]);
        for (int i = 0; i < argc; i++) free(args[i]);
        free(args);
        return 1;
    }
    
    const char* command = args[1];
    
    /* Help command */
    if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 || 
        strcmp(command, "-h") == 0) {
        print_usage(argv[0]);
        for (int i = 0; i < argc; i++) free(args[i]);
        free(args);
        return 0;
    }
    
    /* Version command */
    if (strcmp(command, "version") == 0 || strcmp(command, "--version") == 0 ||
        strcmp(command, "-v") == 0) {
        printf("%s version %s\n", APP_NAME, APP_VERSION);
        for (int i = 0; i < argc; i++) free(args[i]);
        free(args);
        return 0;
    }
    
    /* Daemon command */
    if (strcmp(command, "daemon") == 0) {
        ipfs_node_config_t config;
        ipfs_node_config_default(&config);
        
        /* Parse options */
        for (int i = 2; i < argc; i++) {
            if (strcmp(args[i], "--port") == 0 && i + 1 < argc) {
                config.http_port = (uint16_t)atoi(args[++i]);
            } else if (strcmp(args[i], "--udp-port") == 0 && i + 1 < argc) {
                config.udp_port = (uint16_t)atoi(args[++i]);
            } else if (strcmp(args[i], "--tcp-port") == 0 && i + 1 < argc) {
                config.tcp_port = (uint16_t)atoi(args[++i]);
            } else if (strcmp(args[i], "--data-dir") == 0 && i + 1 < argc) {
                strncpy(config.data_dir, args[++i], sizeof(config.data_dir) - 1);
            } else if (strcmp(args[i], "--quiet") == 0) {
                config.log_level = 1;
            }
        }
        
        /* Create and start node */
        ipfs_node_t* node;
        if (ipfs_node_create(&node, &config) != 0) {
            fprintf(stderr, "Failed to create node\n");
            for (int i = 0; i < argc; i++) free(args[i]);
            free(args);
            return 1;
        }
        
        g_node = node;
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
        
        if (ipfs_node_start(node) != 0) {
            fprintf(stderr, "Failed to start node\n");
            ipfs_node_destroy(node);
            for (int i = 0; i < argc; i++) free(args[i]);
            free(args);
            return 1;
        }
        
        printf("\nNode is running. Press Ctrl+C to stop.\n");
        printf("HTTP Gateway: %s\n", ipfs_node_get_http_url(node));
        printf("\n");
        
        /* Run main loop */
        ipfs_node_run(node);
        
        ipfs_node_destroy(node);
        g_node = NULL;
        for (int i = 0; i < argc; i++) free(args[i]);
        free(args);
        return 0;
    }
    
    /* Add command */
    if (strcmp(command, "add") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Missing file path\n");
            print_usage(argv[0]);
            for (int i = 0; i < argc; i++) free(args[i]);
            free(args);
            return 1;
        }
        
        const char* filepath = args[2];
        
        ipfs_node_config_t config;
        ipfs_node_config_default(&config);
        config.udp_port = 0;
        config.tcp_port = 0;
        config.http_port = 0;
        
        ipfs_node_t* node;
        if (ipfs_node_create(&node, &config) != 0) {
            fprintf(stderr, "Failed to create node\n");
            for (int i = 0; i < argc; i++) free(args[i]);
            free(args);
            return 1;
        }
        
        if (ipfs_node_start(node) != 0) {
            fprintf(stderr, "Failed to start node\n");
            ipfs_node_destroy(node);
            for (int i = 0; i < argc; i++) free(args[i]);
            free(args);
            return 1;
        }
        
        uint8_t cid[IPFS_CID_SIZE];
        if (ipfs_node_add(node, filepath, cid) != 0) {
            fprintf(stderr, "Failed to add file\n");
            ipfs_node_stop(node);
            ipfs_node_destroy(node);
            for (int i = 0; i < argc; i++) free(args[i]);
            free(args);
            return 1;
        }
        
        printf("Added file. CID: ");
        print_cid(cid);
        
        ipfs_node_stop(node);
        ipfs_node_destroy(node);
        for (int i = 0; i < argc; i++) free(args[i]);
        free(args);
        return 0;
    }
    
    /* Get command */
    if (strcmp(command, "get") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Missing CID\n");
            print_usage(argv[0]);
            for (int i = 0; i < argc; i++) free(args[i]);
            free(args);
            return 1;
        }
        
        const char* cid_str = args[2];
        const char* output_path = (argc > 3) ? args[3] : "output";
        
        if (strlen(cid_str) != IPFS_CID_SIZE * 2) {
            fprintf(stderr, "Error: Invalid CID length (expected %d hex chars)\n", 
                    IPFS_CID_SIZE * 2);
            for (int i = 0; i < argc; i++) free(args[i]);
            free(args);
            return 1;
        }
        
        uint8_t cid[IPFS_CID_SIZE];
        for (int i = 0; i < IPFS_CID_SIZE; i++) {
            int high = 0, low = 0;
            char c = cid_str[i * 2];
            if (c >= '0' && c <= '9') high = c - '0';
            else if (c >= 'a' && c <= 'f') high = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') high = c - 'A' + 10;
            else {
                fprintf(stderr, "Error: Invalid CID character '%c'\n", c);
                for (int i = 0; i < argc; i++) free(args[i]);
                free(args);
                return 1;
            }
            
            c = cid_str[i * 2 + 1];
            if (c >= '0' && c <= '9') low = c - '0';
            else if (c >= 'a' && c <= 'f') low = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') low = c - 'A' + 10;
            else {
                fprintf(stderr, "Error: Invalid CID character '%c'\n", c);
                for (int i = 0; i < argc; i++) free(args[i]);
                free(args);
                return 1;
            }
            
            cid[i] = (uint8_t)((high << 4) | low);
        }
        
        ipfs_node_config_t config;
        ipfs_node_config_default(&config);
        config.udp_port = 0;
        config.tcp_port = 0;
        config.http_port = 0;
        
        ipfs_node_t* node;
        if (ipfs_node_create(&node, &config) != 0) {
            fprintf(stderr, "Failed to create node\n");
            for (int i = 0; i < argc; i++) free(args[i]);
            free(args);
            return 1;
        }
        
        if (ipfs_node_start(node) != 0) {
            fprintf(stderr, "Failed to start node\n");
            ipfs_node_destroy(node);
            for (int i = 0; i < argc; i++) free(args[i]);
            free(args);
            return 1;
        }
        
        if (ipfs_node_get(node, cid, output_path) != 0) {
            fprintf(stderr, "Failed to get file\n");
            ipfs_node_stop(node);
            ipfs_node_destroy(node);
            for (int i = 0; i < argc; i++) free(args[i]);
            free(args);
            return 1;
        }
        
        ipfs_node_stop(node);
        ipfs_node_destroy(node);
        for (int i = 0; i < argc; i++) free(args[i]);
        free(args);
        return 0;
    }
    
    /* Pin/Unpin commands - placeholder */
    if (strcmp(command, "pin") == 0 || strcmp(command, "unpin") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Missing CID\n");
            print_usage(argv[0]);
            for (int i = 0; i < argc; i++) free(args[i]);
            free(args);
            return 1;
        }
        printf("Pin/unpin commands are placeholders in this MVP.\n");
        for (int i = 0; i < argc; i++) free(args[i]);
        free(args);
        return 0;
    }
    
    /* Status command */
    if (strcmp(command, "status") == 0) {
        printf("Status command requires a running daemon.\n");
        printf("Use: curl http://localhost:8080/status\n");
        for (int i = 0; i < argc; i++) free(args[i]);
        free(args);
        return 0;
    }
    
    fprintf(stderr, "Error: Unknown command '%s'\n", command);
    print_usage(argv[0]);
    for (int i = 0; i < argc; i++) free(args[i]);
    free(args);
    return 1;
}

/* ANSI main wrapper */
int main(int argc, char* argv[]) {
    /* Convert to wide chars */
    wchar_t** wargv = (wchar_t**)malloc(argc * sizeof(wchar_t*));
    if (!wargv) return 1;
    
    for (int i = 0; i < argc; i++) {
        size_t len = strlen(argv[i]) + 1;
        wargv[i] = (wchar_t*)malloc(len * sizeof(wchar_t));
        if (wargv[i]) {
            mbstowcs(wargv[i], argv[i], len);
        }
    }
    
    int result = wmain(argc, wargv);
    
    for (int i = 0; i < argc; i++) free(wargv[i]);
    free(wargv);
    return result;
}
