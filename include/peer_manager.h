#include <stdint.h>

#define MAX_OUTSTANDING_REQUESTS 10                 // Max number of requests "in-flight" (arbitrary number 10, adjust as needed)

struct Peer {
    int sock_fd;                                    // Socket file descriptor
    uint32_t address;                               // 32-bit IPv4
    uint16_t port;                                  // Port 0-65535
    unsigned char id[20];                           // Hashed peer ID
    
    // Manages if we can upload/download
    int choking;                                    // Choking status, 1 meaning you are choking this peer (you won't upload to it)
    int interested;                                 // This peer is interested if we have pieces it doesn't have

    // Announced by the peer to indicate which pieces it has
    uint8_t *bitfield;                              // Bitmask of pieces this peer has (1 bit -> 1 piece)
    int bitfield_length;                            // Number of bitfield elements

    // Keep track of outstanding requests to this peer
    int num_outstanding;                            // Number of outstanding requests (messages in-flight)
    struct outstanding_request {                    // In-flight request and its fields (kept in queue implemented as a circular array)
        uint32_t index;
        uint32_t begin;
        uint32_t length;
    } outstanding_requests[MAX_OUTSTANDING_REQUESTS];

    // Probably will add more to help handle incoming messages (like an input stream buffer)
};