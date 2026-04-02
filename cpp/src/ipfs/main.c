/**
 * IPFS-like Node - Main Entry Point
 * A.E.T.H.E.R. P2P Protocol
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "ipfs/node.h"

static void print_usage(const char* program) {
    printf("A.E.T.H.E.R. IPFS-like P2P Node\n\n");
    printf("Usage: %s <command> [options]\n\n", program);
    printf("Commands:\n");
    printf("  daemon              Start the IPFS node daemon\n");
    printf("  add <file>          Add a file to the node\n");
    printf("  get <cid> [output]  Get a file by CID\n");
    printf("  pin <cid>           Pin a CID locally\n");
    printf("  unpin <cid>         Unpin a CID\n");
    printf("  status              Show node status\n");
    printf("  help                Show this help message\n");
    printf("\nDaemon Options:\n");
    printf("  --port <num>        HTTP port (default: 8080)\n");
    printf("  --udp-port <num>    UDP port (default: 4001)\n");
    printf("  --tcp-port <num>    TCP port (default: 4002)\n");
    printf("  --data-dir <path>   Data directory (default: ~/.my_ipfs)\n");
    printf("\nExamples:\n");
    printf("  %s daemon                    # Start daemon\n", program);
    printf("  %s add myfile.txt            # Add file, prints CID\n", program);
    printf("  %s get <cid> output.txt      # Get file by CID\n", program);
    printf("\n");
}

static void print_cid(const uint8_t* cid) {
    for (int i = 0; i < IPFS_CID_SIZE; i++) {
        printf("%02x", cid[i]);
    }
    printf("\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char* command = argv[1];
    
    /* Help command */
    if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 || 
        strcmp(command, "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    
    /* Daemon command */
    if (strcmp(command, "daemon") == 0) {
        ipfs_node_config_t config;
        ipfs_node_config_default(&config);
        
        /* Parse options */
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
                config.http_port = (uint16_t)atoi(argv[++i]);
            } else if (strcmp(argv[i], "--udp-port") == 0 && i + 1 < argc) {
                config.udp_port = (uint16_t)atoi(argv[++i]);
            } else if (strcmp(argv[i], "--tcp-port") == 0 && i + 1 < argc) {
                config.tcp_port = (uint16_t)atoi(argv[++i]);
            } else if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
                strncpy(config.data_dir, argv[++i], sizeof(config.data_dir) - 1);
            }
        }
        
        /* Create and start node */
        ipfs_node_t* node;
        if (ipfs_node_create(&node, &config) != 0) {
            fprintf(stderr, "Failed to create node\n");
            return 1;
        }
        
        ipfs_node_set_signal_handler();
        
        if (ipfs_node_start(node) != 0) {
            fprintf(stderr, "Failed to start node\n");
            ipfs_node_destroy(node);
            return 1;
        }
        
        printf("\nNode is running. Press Ctrl+C to stop.\n");
        printf("HTTP Gateway: %s\n", ipfs_node_get_http_url(node));
        printf("\n");
        
        /* Run main loop */
        ipfs_node_run(node);
        
        ipfs_node_destroy(node);
        return 0;
    }
    
    /* Commands that need a running node */
    if (strcmp(command, "add") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Missing file path\n");
            print_usage(argv[0]);
            return 1;
        }
        
        const char* filepath = argv[2];
        
        /* For add/get/pin/unpin, we need to connect to running daemon */
        /* For MVP, we'll create a temporary node */
        ipfs_node_config_t config;
        ipfs_node_config_default(&config);
        
        /* Use different ports to avoid conflict with daemon */
        config.udp_port = 0;  /* Let OS choose */
        config.tcp_port = 0;
        config.http_port = 0;
        
        ipfs_node_t* node;
        if (ipfs_node_create(&node, &config) != 0) {
            fprintf(stderr, "Failed to create node\n");
            return 1;
        }
        
        if (ipfs_node_start(node) != 0) {
            fprintf(stderr, "Failed to start node\n");
            ipfs_node_destroy(node);
            return 1;
        }
        
        uint8_t cid[IPFS_CID_SIZE];
        if (ipfs_node_add(node, filepath, cid) != 0) {
            fprintf(stderr, "Failed to add file\n");
            ipfs_node_stop(node);
            ipfs_node_destroy(node);
            return 1;
        }
        
        printf("Added file. CID: ");
        print_cid(cid);
        
        ipfs_node_stop(node);
        ipfs_node_destroy(node);
        return 0;
    }
    
    if (strcmp(command, "get") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Missing CID\n");
            print_usage(argv[0]);
            return 1;
        }
        
        const char* cid_str = argv[2];
        const char* output_path = (argc > 3) ? argv[3] : "output";
        
        /* Parse CID from hex */
        if (strlen(cid_str) != IPFS_CID_SIZE * 2) {
            fprintf(stderr, "Error: Invalid CID length (expected %d hex chars)\n", 
                    IPFS_CID_SIZE * 2);
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
                return 1;
            }
            
            c = cid_str[i * 2 + 1];
            if (c >= '0' && c <= '9') low = c - '0';
            else if (c >= 'a' && c <= 'f') low = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') low = c - 'A' + 10;
            else {
                fprintf(stderr, "Error: Invalid CID character '%c'\n", c);
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
            return 1;
        }
        
        if (ipfs_node_start(node) != 0) {
            fprintf(stderr, "Failed to start node\n");
            ipfs_node_destroy(node);
            return 1;
        }
        
        if (ipfs_node_get(node, cid, output_path) != 0) {
            fprintf(stderr, "Failed to get file\n");
            ipfs_node_stop(node);
            ipfs_node_destroy(node);
            return 1;
        }
        
        ipfs_node_stop(node);
        ipfs_node_destroy(node);
        return 0;
    }
    
    if (strcmp(command, "pin") == 0 || strcmp(command, "unpin") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Missing CID\n");
            print_usage(argv[0]);
            return 1;
        }
        
        /* For MVP, just acknowledge the command */
        printf("Pin/unpin commands are placeholders in this MVP.\n");
        return 0;
    }
    
    if (strcmp(command, "status") == 0) {
        /* Status would need to connect to running daemon via HTTP or IPC */
        printf("Status command requires a running daemon.\n");
        printf("Use: curl http://localhost:8080/status\n");
        return 0;
    }
    
    fprintf(stderr, "Error: Unknown command '%s'\n", command);
    print_usage(argv[0]);
    return 1;
}
