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

// send GET request to get list of peers
// calls internal udp or http(s) helpers depending on protocol specified in announce URL
TrackerResponse tracker_get(char *announce, unsigned char *info_hash, unsigned char *peer_id, 
    int port, long uploaded, long downloaded, long left);

// scrape convention, calls internal helpers based on protocol
// returns -1 if scrape is not supported for this tracker, 0 on success
int scrape(TrackerResponse *response, char *announce, unsigned char *info_hash);

// free list of peers
void free_tracker_response(TrackerResponse *response);