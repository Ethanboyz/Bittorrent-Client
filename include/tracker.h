#include "peer_manager.h"
#include "bencode.h"

// file for tracker communication
// goal here is to get the peer list so our client can participate in the torrent

// NOTE: optional parameters have been left out for now

typedef struct {
    unsigned char info_hash[20];
    unsigned char peer_id[20];
    int port;
    int uploaded;
    int downloaded;
    int left;
    int compact;
    char event[10];
} TrackerRequest;

typedef struct {
    int interval;
    int complete;
    int incomplete;
    Peer *peers;
} TrackerResponse;

// format URL
// send HTTP GET request 
    // response is a bencoded dict