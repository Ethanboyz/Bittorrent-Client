#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <argp.h>
#include <time.h>
#include <sys/select.h>
#include <poll.h>
#include <netinet/tcp.h>
#include <stdbool.h>

#include "peer_manager.h"
#include "btclient.h"

Peer peers[MAX_PEERS];

// Send handshake message to peer
// TODO: implement this!
static int send_handshake(Peer *peer) {
    return 0;
}

// Add and connect to a new peer
// TODO: implement this!
int add_peer(const struct sockaddr_in *addr, socklen_t addr_len) {
    return 0;
}

// Disconnect and remove a specified peer
// TODO: implement this!
int remove_peer(Peer *peer) {
    return 0;
}

// Send messages to peer
// TODO: implement this!
int send_to_peer(Peer *peer, unsigned char messages[]) {
    return 0;
}

// Receive messages from peer, if any are present
// TODO: implement this!
int receive_from_peer(Peer *peer, unsigned char messages[]) {
    return 0;
}

// Send a keepalive message to peer
// TODO: implement this!
int send_keepalive_message(const Peer *peer) {
    return 0;
}