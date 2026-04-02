/**
 * A.E.T.H.E.R. Node CLI
 * Command-line interface for running A.E.T.H.E.R. nodes.
 */

#include "aether.hpp"
#include "protocol.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>

using namespace aether;

// Helper function to convert NodeId to hex string
static std::string node_id_to_hex(const NodeId& id) {
    static const char hex_chars[] = "0123456789abcdef";
    std::ostringstream oss;
    for (uint8_t byte : id) {
        oss << hex_chars[(byte >> 4) & 0xF] << hex_chars[byte & 0xF];
    }
    return oss.str();
}

static std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

void print_banner() {
    std::cout << "============================================================" << std::endl;
    std::cout << "     A.E.T.H.E.R. Node" << std::endl;
    std::cout << "  Asynchronous Edge-Tolerant Holographic" << std::endl;
    std::cout << "       Execution Runtime v" << VERSION_STRING << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << std::endl;
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p, --port <port>       Listening port (default: 7821)" << std::endl;
    std::cout << "  -d, --datadir <dir>     Data directory" << std::endl;
    std::cout << "  -v, --verbose           Verbose logging" << std::endl;
    std::cout << "  -h, --help              Show this help" << std::endl;
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  run                     Run a node (default)" << std::endl;
    std::cout << "  connect                 Connect to a node" << std::endl;
    std::cout << std::endl;
}

int cmd_run(int argc, char* argv[]) {
    print_banner();

    Config config;

    // Parse arguments
    for (int i = 0; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                config.listen_port = static_cast<uint16_t>(std::stoul(argv[++i]));
            }
        } else if (arg == "-d" || arg == "--datadir") {
            if (i + 1 < argc) {
                config.data_dir = argv[++i];
            }
        } else if (arg == "-v" || arg == "--verbose") {
            config.log_level = LogLevel::Debug;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Set identity path
    if (!config.data_dir.empty()) {
        config.identity_path = config.data_dir + "/identity.bin";
    }

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Create and start node
    Node node(config);

    if (auto err = node.start(); err != Error::Ok) {
        std::cerr << "Failed to start node: " << error_string(err) << std::endl;
        return 1;
    }

    std::cout << "Configuration:" << std::endl;
    std::cout << "  Listen port:     " << config.listen_port << std::endl;
    std::cout << "  Max connections: " << config.max_connections << std::endl;
    std::cout << "  Data directory:  " << config.data_dir << std::endl;
    std::cout << "  Log level:       " << static_cast<int>(config.log_level) << std::endl;
    std::cout << std::endl;

    std::cout << "Node ID: " << node_id_to_hex(node.node_id()).substr(0, 16) << "..." << std::endl;
    std::cout << "Listening on port " << node.port() << std::endl;
    std::cout << std::endl;
    std::cout << "Press Ctrl+C to stop..." << std::endl;
    std::cout << std::endl;

    // Run main loop
    std::thread run_thread([&node]() {
        node.run();
    });

    // Wait for shutdown signal
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\nShutting down..." << std::endl;

    node.stop();
    run_thread.join();

    std::cout << "A.E.T.H.E.R. Node stopped." << std::endl;
    return 0;
}

int cmd_connect(int argc, char* argv[]) {
    std::string host = "localhost";
    uint16_t port = DEFAULT_PORT;
    bool ping = false;
    bool interactive = false;
    std::string store_key, store_value;
    std::string get_key;
    bool peers = false;

    // Parse arguments
    for (int i = 0; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-H" || arg == "--host") {
            if (i + 1 < argc) {
                host = argv[++i];
            }
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = static_cast<uint16_t>(std::stoul(argv[++i]));
            }
        } else if (arg == "-i" || arg == "--interactive") {
            interactive = true;
        } else if (arg == "--ping") {
            ping = true;
        } else if (arg == "--store" && i + 2 < argc) {
            store_key = argv[++i];
            store_value = argv[++i];
        } else if (arg == "--get" && i + 1 < argc) {
            get_key = argv[++i];
        } else if (arg == "--peers") {
            peers = true;
        }
    }

    std::cout << "============================================================" << std::endl;
    std::cout << "     A.E.T.H.E.R. Client v" << VERSION_STRING << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << std::endl;

    Client client(host, port);

    if (auto err = client.connect(); err != Error::Ok) {
        std::cout << "\nFailed to connect. Is the node running?" << std::endl;
        return 1;
    }

    try {
        if (interactive) {
            std::cout << "Interactive mode. Commands: ping, store, get, find, peers, quit" << std::endl;
            std::cout << std::endl;

            std::string line;
            while (true) {
                std::cout << "aether> ";
                std::getline(std::cin, line);

                if (line == "quit" || line == "exit") {
                    break;
                } else if (line == "ping") {
                    double latency = client.ping();
                    if (latency >= 0) {
                        std::cout << "[+] PONG received (latency: " << latency << "ms)" << std::endl;
                    } else {
                        std::cout << "[-] No PONG received" << std::endl;
                    }
                } else if (line.rfind("store ", 0) == 0 && line.length() > 6) {
                    size_t space = line.find(' ', 6);
                    if (space != std::string::npos) {
                        std::string key = line.substr(6, space - 6);
                        std::string value = line.substr(space + 1);
                        if (client.store(key, value) == Error::Ok) {
                            std::cout << "[+] Stored value for key: " << key << std::endl;
                        }
                    }
                } else if (line.rfind("get ", 0) == 0 && line.length() > 4) {
                    std::string key = line.substr(4);
                    auto result = client.get(key);
                    if (result) {
                        std::cout << "[+] Found value: " << *result << std::endl;
                    } else {
                        std::cout << "[-] Key not found: " << key << std::endl;
                    }
                } else if (line == "peers") {
                    auto peers = client.peer_exchange();
                    std::cout << "[+] Got " << peers.size() << " peers:" << std::endl;
                    for (const auto& ep : peers) {
                        std::cout << "    - " << ep.to_string() << std::endl;
                    }
                } else if (!line.empty()) {
                    std::cout << "Unknown command." << std::endl;
                }
            }
        } else if (ping) {
            double latency = client.ping();
            if (latency >= 0) {
                std::cout << "[+] PONG received (latency: " << latency << "ms)" << std::endl;
            } else {
                std::cout << "[-] No PONG received" << std::endl;
            }
        } else if (!store_key.empty()) {
            if (client.store(store_key, store_value) == Error::Ok) {
                std::cout << "[+] Stored value for key: " << store_key << std::endl;
            }
        } else if (!get_key.empty()) {
            auto result = client.get(get_key);
            if (result) {
                std::cout << "[+] Found value: " << *result << std::endl;
            } else {
                std::cout << "[-] Key not found: " << get_key << std::endl;
            }
        } else if (peers) {
            auto peers = client.peer_exchange();
            std::cout << "[+] Got " << peers.size() << " peers:" << std::endl;
            for (const auto& ep : peers) {
                std::cout << "    - " << ep.to_string() << std::endl;
            }
        } else {
            // Default: just ping
            double latency = client.ping();
            if (latency >= 0) {
                std::cout << "[+] PONG received (latency: " << latency << "ms)" << std::endl;
            }
            std::cout << "\n[*] Use --help for available commands" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    client.disconnect();
    std::cout << "[*] Disconnected" << std::endl;

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "run" || cmd == "-p" || cmd == "--port" || cmd == "-d" || cmd == "--datadir" ||
        cmd == "-v" || cmd == "--verbose" || cmd == "-h" || cmd == "--help") {
        return cmd_run(argc - 1, argv + 1);
    } else if (cmd == "connect") {
        return cmd_connect(argc - 1, argv + 1);
    } else if (cmd == "-h" || cmd == "--help") {
        print_usage(argv[0]);
        return 0;
    } else {
        std::cerr << "Unknown command: " << cmd << std::endl;
        print_usage(argv[0]);
        return 1;
    }
}
