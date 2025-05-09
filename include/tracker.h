#include "peer_manager.h"
#include "bencode.h"

// tracker communication

// NOTE: optional parameters have been LEFT OUT for now

typedef struct {
    int interval;
    int complete;
    int incomplete;
    int num_peers;
    Peer *peers;
} TrackerResponse;

// send HTTP or HTTPS GET request
TrackerResponse http_get(char *announce, unsigned char *info_hash, unsigned char *peer_id, 
    int port, long uploaded, long downloaded, long left);

// free list of peers
void free_tracker_response(TrackerResponse *response);