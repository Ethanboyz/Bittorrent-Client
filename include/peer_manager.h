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

#define DEFAULT_BLOCK_LENGTH 16384 // MOD: added this from piece_manager.h to remove make error due to calculation of MAX_INCOMING_BYTES

#define MAX_OUTSTANDING_REQUESTS 10                 // Max number of requests "in-flight" per peer (arbitrary number 10, adjust as needed)
#define MAX_PEERS 50                                // Max number of peers per torrent

// Max number of incoming bytes based on the size of piece messages
#define MAX_INCOMING_BYTES (MAX_OUTSTANDING_REQUESTS * (DEFAULT_BLOCK_LENGTH + 17))

typedef struct {
    // Announced by the peer to indicate which pieces it has
    unsigned char *bitfield;                        // Bitmask of pieces this peer has (1 bit -> 1 piece)
    size_t bitfield_bytes;                          // Number of bitfield bytes

    // Buffer for incoming messages per peer to make message parsing easier
    unsigned char incoming_buffer[MAX_INCOMING_BYTES];
    size_t incoming_buffer_offset;                  // Bytes in use in incoming_buffer

    // Download/upload rate fields
    ssize_t bytes_sent;                             // Bytes sent since the last rate measure
    ssize_t bytes_recv;                             // Bytes received since the last rate measure
    struct timeval last_rate_time;                  // Last time a rate measure was taken
    double upload_rate;                             // Last measured upload rate (bits/sec)
    double download_rate;                           // Last measured download rate (bits/sec)

    // For keepalive
    time_t last_keepalive_to_peer;                  // The last time a keepalive was sent to this peer

    // The torrent that this peer is associated with
    Torrent torrent;

    // Keep track of our outstanding requests to this peer
    int num_outstanding_requests;                   // Number of outstanding requests (messages in-flight)
    int requests_tail, requests_head;               // Tail = empty index to be enqueued. Head = filled index to be dequeued
    struct request {                                // In-flight request and its fields (kept in queue implemented as a circular array)
        uint32_t index;
        uint32_t begin;
        uint32_t length;
    } outstanding_requests[MAX_OUTSTANDING_REQUESTS];

    // Connectivity info
    int sock_fd;                                    // Socket file descriptor
    uint32_t address;                               // 32-bit IPv4 (big endian/network byte order)
    uint16_t port;                                  // Port 0-65535 (big endian/network byte order)
    unsigned char id[20];                           // Unique peer ID
    bool we_initiated;                              // True if we initiated the connection, false if the peer initiated with us
    
    // Manages if we can upload/download
    bool handshake_done;                            // True meaning handshake exchange is complete with this peer
    bool choking;                                   // True meaning you are choking this peer (you won't upload to it)
    bool is_interesting;                            // True meaning we are interested in this peer (it has pieces we need)
    bool choked;                                    // True meaning this peer is choking us (don't bother sending requests to it)
    bool is_interested;                             // True meaning this peer is interested in us (we have pieces it doesn't have)
} Peer;

/**
 * @brief Send cancel message to peer
 * @return 0 if successful, -1 otherwise
 */
int peer_manager_send_cancel(Peer *peer, uint32_t request_index, uint32_t request_begin, uint32_t request_length);

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
 * @brief Get the last time a keepalive message was sent to peer (this should ideally not exceed 120 seconds)
 * @return Seconds since the last keepalive message was sent to peer
 */
double peer_manager_last_keepalive_message(Peer *peer);

/**
 * @brief Send a keepalive message to peer (try to send this every two minutes or less per peer)
 * @return 0 if successful, -1 otherwise
 */ 
int peer_manager_send_keepalive_message(Peer *peer);

/**
 * @brief Receive incoming, store in buffer, and process them accordingly. For example, request messages prompt the client to send pieces if possible
 * @return Number of bytes received, 0 if the peer has been disconnected (recommend to call peer_manager_remove_peer and decrement index if in a loop),
 * or -1 upon error (likely due to no available messages) */
int peer_manager_receive_messages(Peer *peer);

/**
 * @brief Add and connect to a new peer specified by the given address and length, then send it a handshake.
 * If addr is NULL, will attempt to add any peers via incoming connections instead, then send it a handshake.
 * @return The connected peer's socket file descriptor, 0 if there was no new peer when addr is NULL or if peer at addr connection attempt timed out, or -1 if failed to connect
 */
int peer_manager_add_peer(Torrent torrent, const struct sockaddr_in *addr, socklen_t addr_len);

/**
 * @brief Disconnect and remove a specified peer. Compacts the fds and peers arrays by filling the resulting empty hole when the peer is removed.
 * @return 0 if successful, -1 otherwise
 */
int peer_manager_remove_peer(Peer *peer);

/**
 * @brief Updates the download and upload rates. If you want the result rates, call get_download_rate() or get_upload_rate()
 * @param peer The peer whose rates will be updated
 * @return 0 if successful, -1 otherwise
 */
int update_download_upload_rate(Peer *peer);

/**
 * @brief Get the last-updated download rates of peer.
 * @return The download rate, in bits/sec
 */
double get_download_rate(Peer *peer);

/**
 * @brief Get the last-updated upload rates of peer.
 * @return The upload rate, in bits/sec
 */
double get_upload_rate(Peer *peer);

void peer_manager_send_keep_alives(void);

#endif