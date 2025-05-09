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
// TODO: create new peer and set it to peer[num_peers]!
int add_peer(const struct sockaddr_in *addr, socklen_t addr_len) {
    int new_sock;
    struct sockaddr_in client_addr;
    socklen_t addr_size = sizeof(client_addr);
    memset(&client_addr, 0, sizeof(client_addr));   // Initialize

    struct pollfd *fds = get_fds();
    Peer *peers = get_peers();
    int *num_fds = get_num_fds();
    int *num_peers = get_num_peers();

    // Check if any new peers are connecting
    if (fds[0].revents & POLLIN) {
        new_sock = accept(fds[0].fd, (struct sockaddr*)&client_addr, &addr_size);
        if (new_sock == -1) {
            return -1;
        }

        // Add the new peers socket to the pollfd array and peers array
        fds[*num_fds].fd = new_sock;
        fds[*num_fds].events = POLLIN;
        num_fds++;
        
        // TODO: create new peer and set it to peer[num_peers];

        num_peers++;
        
        if (get_args().debug_mode) fprintf(stderr, "New connection from %s on socket %d\n", inet_ntoa(client_addr.sin_addr), new_sock);
        return new_sock;
    }
    return 0;
}

// Disconnect and remove a specified peer. Compacts the fds and peers arrays by filling the resulting empty hole when the peer is removed.
// TODO: confirm there is nothing to do before disconnecting the peer, and free any fields in peers[(fds_index) - 1] if needed!
int remove_peer(Peer *peer) {
    struct pollfd *fds = get_fds();
    Peer *peers = get_peers();
    int *num_fds = get_num_fds();
    int *num_peers = get_num_peers();

    int fds_index = -1;
    for (int i = 0; i < *num_peers; i++) {
        if (peer->id == peers[i].id) {
            fds_index = i - 1;
        }
    }

    if (fds_index == -1) {
        if (get_args().debug_mode) fprintf(stderr, "Attempted to remove a peer that did not exist\n");
        return -1;
    }

    // TODO: make sure there is nothing else to do before disconnecting the peer

    close(fds[fds_index].fd);   // Disconnecting the peer

    // The entry is now empty, so compact the fds and clients array (fill in the empty space)
    num_fds--;
    fds[fds_index].fd = fds[*num_fds].fd;
    fds[fds_index].events = fds[*num_fds].events;
    fds[fds_index].revents = fds[*num_fds].revents;

    // TODO: free any fields in peers[(fds_index) - 1], if needed, since it will be replaced (we don't want memory leaks)

    peers[(fds_index) - 1] = peers[*num_peers - 1];
    (*num_peers)--;
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