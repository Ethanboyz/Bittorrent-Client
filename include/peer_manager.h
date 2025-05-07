#ifndef PEER_MANAGER_H
#define PEER_MANAGER_H

#include <unistd.h>
#include <arpa/inet.h>
#include <argp.h>
#include <sys/select.h>
#include <poll.h>
#include <netinet/tcp.h>
#include <stdbool.h>

#include "torrent_parser.h"

#define MAX_OUTSTANDING_REQUESTS 10                 // Max number of requests "in-flight" per peer (arbitrary number 10, adjust as needed)
#define MAX_PEERS 65535                             // Max number of peers per torrent

typedef struct {
    int sock_fd;                                    // Socket file descriptor
    uint32_t address;                               // 32-bit IPv4
    uint16_t port;                                  // Port 0-65535
    unsigned char id[20];                           // Unique peer ID
    
    // Manages if we can upload/download
    bool choking;                                   // Choking status, 1 meaning you are choking this peer (you won't upload to it)
    bool interested;                                // This peer is interested if we have pieces it doesn't have

    // Announced by the peer to indicate which pieces it has
    uint8_t *bitfield;                              // Bitmask of pieces this peer has (1 bit -> 1 piece)
    size_t bitfield_length;                         // Number of bitfield elements

    // Keep track of outstanding requests to this peer
    int num_outstanding_requests;                   // Number of outstanding requests (messages in-flight)
    struct request {                                // In-flight request and its fields (kept in queue implemented as a circular array)
        uint32_t index;
        uint32_t begin;
        uint32_t length;
    } outstanding_requests[MAX_OUTSTANDING_REQUESTS];

    // Buffer for incoming messages per peer to make message parsing easier
    unsigned char *incoming_buffer;                 // Buffer usage will allow processing incoming messages however they come
    size_t incoming_buffer_len;
} Peer;

/**
 * @brief Add and connect to a new peer and immediately send it a handshake
 * @return The connected peer's socket file descriptor, or -1 if failed to connect
 */
int add_peer(const struct sockaddr_in *addr, socklen_t addr_len);

/**
 * @brief Disconnect and remove a specified peer. Compacts the fds and peers arrays by filling the resulting empty hole when the peer is removed.
 * @return 0 if successful, -1 otherwise
 */
int remove_peer(Peer *peer);

/**
 * @brief Send a keepalive message to peer
 * @return 0 if successful, -1 otherwise
 */
int send_keepalive_message(const Peer *peer);

#endif