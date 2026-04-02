/**
 * STUN Client for NAT Hole Punching
 * Header file
 */

#ifndef IPFS_STUN_H
#define IPFS_STUN_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Discover public IP and port via STUN
 * @param public_ip Output buffer for public IP address
 * @param ip_len Length of public_ip buffer (at least 16 bytes)
 * @param public_port Output port number
 * @param stun_server STUN server address (e.g., "stun.l.google.com:19302")
 * @return 0 on success, -1 on failure
 */
int stun_discover_public_ip(char* public_ip, size_t ip_len, uint16_t* public_port,
                             const char* stun_server);

/**
 * Get public IP by trying multiple STUN servers
 * @param public_ip Output buffer for public IP address
 * @param ip_len Length of public_ip buffer
 * @param public_port Output port number
 * @return 0 on success, -1 on failure
 */
int stun_get_public_ip(char* public_ip, size_t ip_len, uint16_t* public_port);

/**
 * Get local IP address
 * @param local_ip Output buffer for local IP address
 * @param ip_len Length of local_ip buffer
 * @return 0 on success
 */
int stun_get_local_ip(char* local_ip, size_t ip_len);

#ifdef __cplusplus
}
#endif

#endif /* IPFS_STUN_H */
