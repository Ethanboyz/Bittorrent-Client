#ifndef PEER_MANAGER_H
#define PEER_MANAGER_H

#include <unistd.h>
#include <arpa/inet.h>
#include <argp.h>
#include <sys/select.h>
#include <poll.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <time.h>

#include "torrent_parser.h"
#include "piece_manager.h"

#define MAX_OUTSTANDING_REQUESTS 10                 // Max number of requests "in-flight" per peer (arbitrary number 10, adjust as needed)
#define MAX_PEERS 65535                             // Max number of peers per torrent

// Max number of incoming bytes based on the size of piece messages
#define MAX_INCOMING_BYTES (MAX_OUTSTANDING_REQUESTS * (DEFAULT_BLOCK_LENGTH + 17))

typedef struct {
    // Announced by the peer to indicate which pieces it has
    unsigned char *bitfield;                        // Bitmask of pieces this peer has (1 bit -> 1 piece)
    size_t bitfield_bits;                           // Number of bitfield elements (bits)

    // Buffer for incoming messages per peer to make message parsing easier
    unsigned char incoming_buffer[MAX_INCOMING_BYTES];
    size_t incoming_buffer_offset;                  // Bytes in use in incoming_buffer

    // The torrent that this peer is associated with
    Torrent torrent;

    // Connectivity info
    int sock_fd;                                    // Socket file descriptor
    uint32_t address;                               // 32-bit IPv4 (big endian/network byte order)
    uint16_t port;                                  // Port 0-65535 (big endian/network byte order)
    unsigned char id[20];                           // Unique peer ID

    // Download/upload rate fields
    ssize_t bytes_sent;                             // Bytes sent since the last rate measure
    ssize_t bytes_recv;                             // Bytes received since the last rate measure
    struct timeval last_rate_time;                 // Last time a rate measure was taken
    double upload_rate;                             // Last measured upload rate
    double download_rate;                           // Last measured download rate

    // Keep track of our outstanding requests to this peer
    int num_outstanding_requests;                   // Number of outstanding requests (messages in-flight)
    int requests_tail, requests_head;               // Tail = empty index to be enqueued. Head = filled index to be dequeued
    struct request {                                // In-flight request and its fields (kept in queue implemented as a circular array)
        uint32_t index;
        uint32_t begin;
        uint32_t length;
    } outstanding_requests[MAX_OUTSTANDING_REQUESTS];
    
    // Manages if we can upload/download
    bool choking;                                   // True meaning you are choking this peer (you won't upload to it)
    bool is_interesting;                            // True meaning we are interested in this peer (it has pieces we need)
    bool choked;                                    // True meaning this peer is choking us (don't bother sending requests to it)
    bool is_interested;                             // True meaning this peer is interested in us (we have pieces it doesn't have)
} Peer;

/** 
 * @brief Send interested message to peer
 * @return 0 if successful, -1 otherwise
 */
int peer_manager_send_interested(Peer *peer);

/** 
 * @brief Send not interested message to peer
 * @return 0 if successful, -1 otherwise
 */
int peer_manager_send_not_interested(Peer *peer);

/** 
 * @brief Choke and send choke message to peer
 * @return 0 if successful, -1 otherwise
 */
int peer_manager_choke_peer(Peer *peer);

/** 
 * @brief Unchoke and send unchoke message to peer
 * @return 0 if successful, -1 otherwise
 */
int peer_manager_unchoke_peer(Peer *peer);

/**
 * @brief Send bitfield to peer 
 * @return 0 if successful, -1 otherwise
 */
int send_bitfield(Peer *peer);

/**
 * @brief Sends the request and queues it as an outstanding request and will be stored for reference until a corresponding piece is received.
 * @return 0 if successful, -1 if message is not sent (there are too many outstanding requests for this peer).
 */
int peer_manager_send_request(Peer *peer, uint32_t request_index, uint32_t request_begin, uint32_t request_length);

/**
 * @brief Send a keepalive message to peer
 * @return 0 if successful, -1 otherwise
 */ 
int peer_manager_send_keepalive_message(Peer *peer);

/**
 * @brief Receive incoming, store in buffer, and process
 * @return Number of bytes received, 0 if the peer has disconnected (recommend to call peer_manager_remove_peer and decrement index if in a loop),
 * or -1 upon error (likely due to no available messages) */
int peer_manager_receive_messages(Peer *peer);

/**
 * @brief Add and connect to a new peer specified by the given address and length, then send it a handshake.
 * If addr is NULL, will attempt to add any peers via incoming connections instead, then send it a handshake.
 * @return The connected peer's socket file descriptor, 0 if there was no new peer (if addr is NULL), or -1 if failed to connect
 */
int peer_manager_add_peer(Torrent torrent, const struct sockaddr_in *addr, socklen_t addr_len);

/**
 * @brief Disconnect and remove a specified peer. Compacts the fds and peers arrays by filling the resulting empty hole when the peer is removed.
 * @return 0 if successful, -1 otherwise
 */
int peer_manager_remove_peer(Peer *peer);

/**
 * @brief Send a keepalive message to peer
 * @return 0 if successful, -1 otherwise
 */
int send_keepalive_message(Peer *peer);

#endif