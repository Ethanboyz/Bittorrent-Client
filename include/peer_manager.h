#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_OUTSTANDING_REQUESTS 10                 // Max number of requests "in-flight" (arbitrary number 10, adjust as needed)

struct Peer {
    int sock_fd;                                    // Socket file descriptor
    uint32_t address;                               // 32-bit IPv4
    uint16_t port;                                  // Port 0-65535
    unsigned char id[20];                           // Hashed peer ID
    
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
};