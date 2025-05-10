#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <argp.h>
#include <sys/time.h>
#include <sys/select.h>
#include <poll.h>
#include <netinet/tcp.h>
#include <stdbool.h>

#include "peer_manager.h"
#include "btclient.h"
#include "torrent_parser.h"
#include "piece_manager.h"

enum MSG_ID {
    CHOKE,
    UNCHOKE,
    INTERESTED,
    NOT_INTERESTED,
    HAVE,
    BITFIELD,
    REQUEST,
    PIECE,
    CANCEL,
    PORT
};

static const char *PROTOCOL = "BitTorrent protocol";

// Send a message via socket fd, returning the number of bytes sent (helper function)
static int send_message(Peer *peer, const unsigned char *message, size_t message_len) {
    size_t sent = 0;
    // Avoid potential partial writes to send buffer by cumulatively sending the message multiple times if needed
    while (sent < message_len) {
        int n = send(peer->sock_fd, message + sent, message_len - sent, 0);
        if (n == -1) {
            if (errno == EINTR) {
                if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Message did not send due to interruption, trying again...\n");
                continue;
            }
            return -1;
        }
        sent += n;
        peer->bytes_sent += n;
    }
    return sent;
}

// Send handshake message to peer with info_hash attached, returning the number of bytes sent
static int send_handshake(Peer *peer) {
    int handshake_length = 68;
    unsigned char message[handshake_length];
    int offset = 0;                         // Cumulatively construct the message

    // pstrlen and pstr
    message[offset] = (uint8_t)strlen(PROTOCOL);
    offset++;
    memcpy(message + offset, PROTOCOL, strlen(PROTOCOL));
    offset += strlen(PROTOCOL);

    // Reserved 8 bytes of all 0s
    memset(message + offset, 0, 8);
    offset += 8;

    // Info_hash and peer_id
    memcpy(message + offset, torrent_get_info_hash(&peer->torrent), 20);
    offset += 20;
    memcpy(message + offset, &peer->id, 20);
    offset += 20;

    if (offset != handshake_length) {
        if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Something went wrong while constructing handshake\n");
        return -1;
    }

    int sent = send_message(peer, message, handshake_length);
    if (sent == -1 && get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Failed to send handshake\n");

    return sent;
}

// Dequeue an outstanding request for peer (when a response is confirmed for that request), and write the response data
// TODO: write the actual data to wherever it needs to go!
static void dequeue_and_process_outstanding(Peer *peer, uint32_t piece_index, uint32_t piece_begin, const uint8_t *block, size_t length) {
    int found_index = -1;
    for (int i = 0; i < peer->num_outstanding_requests; i++) {              // Search for the outstanding_request index with the matching request
        int index = (peer->requests_head + i) % MAX_OUTSTANDING_REQUESTS;   // Remember that we're working with a circular array here
        struct request element = peer->outstanding_requests[index];
        if (element.index == piece_index && element.begin == piece_begin) {    // CONFIRM
            found_index = index;
            break;
        }
    }
    if (found_index == -1) {
        if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Dequeue outstanding request failed. No record of request found\n");
        return;
    }

    // TODO: write block with length "length" at piece_index, piece_begin in file

    // Dequeue the request element, shift everything to fill the empty hole
    int curr_index = found_index;
    while (curr_index != peer->requests_head) {
        int prev_index = (curr_index - 1 + MAX_OUTSTANDING_REQUESTS) % MAX_OUTSTANDING_REQUESTS;
        peer->outstanding_requests[curr_index] = peer->outstanding_requests[prev_index];
        curr_index = prev_index;
    }
    peer->requests_head = (peer->requests_head + 1) % MAX_OUTSTANDING_REQUESTS;
    peer->num_outstanding_requests--;
}

// Handle a single message (with length prefix attached)
// TODO: implement msg_id cases!
static void handle_peer_message(Peer *peer, uint8_t msg_id, const uint8_t *payload, size_t payload_length) {
    switch (msg_id) {
        case CHOKE: {
            peer->choked = true;
            break;
        }
        case UNCHOKE: {
            peer->choked = false;
            break;
        }
        case INTERESTED: {
            // TODO: implement this!
            break;
        }
        case NOT_INTERESTED: {
            // TODO: implement this!
            break;
        }
        case HAVE: {
            // Consider simply disregarding completely
            break;
        }
        case BITFIELD: {
            memcpy(peer->bitfield, payload, payload_length);
            // TODO: set bitfield bits!
            break;
        }
        case REQUEST: {
            // TODO: implement this!
            break;
        }
        case PIECE: {
            // Break down the piece message, then process it
            if (payload_length >= 0) {
                uint32_t index = 0;
                memcpy(&index, payload + 0, 4);
                index = ntohl(index);
                uint32_t begin = 0;
                memcpy(&begin, payload + 4, 4);
                begin = ntohl(begin);
                const unsigned char *block = payload + 8;
                size_t block_length = payload_length - 8;   // 8 is the length of index and begin combined
                dequeue_and_process_outstanding(peer, index, begin, block, block_length);
            }
            break;
        }
        case CANCEL: {
            // Ignore this for now, this is for End Game
            break;
        }
        case PORT: {
            // Ignore this for now, this is for DHT
            break;
        }
    }
}

// Parse as many full messages from incoming_buffer as possible
// TODO: needs to handle handshake messages!
static void parse_peer_incoming_buffer(Peer *peer) {
    size_t offset = 0;                                          // For keeping track of what was processed
    size_t available_bytes = peer->incoming_buffer_offset;      // How many bytes are left
    while (available_bytes >= 4) {
        uint32_t length_prefix;
        memcpy(&length_prefix, peer->incoming_buffer + offset, 4);
        length_prefix = ntohl(length_prefix);

        if (length_prefix == 0) {           // Message was a keepalive, continue
            offset += 4;
            available_bytes -= 4;
            continue;
        }
        if (available_bytes < 4 + length_prefix) {
            // Ran out of data, cannot process anymore messages
            break;
        }

        if (length_prefix == 19) {                      // Message was a handshake
            char protocol[20];
            strncpy(protocol, peer->incoming_buffer + 1, 20);
            if (!strncmp(protocol, PROTOCOL, 19)) {      // Confirm it's actually a handshake
                // TODO: get info_hash and peer_id
                // TODO: if info_hash is unverifiable, drop connection (?), else, send bitfield
            }
        }

        // Full normal message available, process it
        uint8_t msg_id = peer->incoming_buffer[offset + 4];
        unsigned char *payload = peer->incoming_buffer + offset + 5;
        size_t payload_length = length_prefix - 1;
        handle_peer_message(peer, msg_id, payload, payload_length);

        size_t full_message_length = 4 + length_prefix;
        offset += full_message_length;
        available_bytes -= full_message_length;
    }

    // Once done processing all possible messages, slide potential leftovers to the beginning of incoming_buffer
    if (offset > 0) {
        memmove(peer->incoming_buffer, peer->incoming_buffer + offset, available_bytes);    // The discovery of this function is revolutionary 0_0
        peer->incoming_buffer_offset = available_bytes;                                     // Next reads will be written after the leftover bytes
    }
}

// Send interested message to peer
int peer_manager_send_interested(Peer *peer) {
    uint8_t message[5];
    uint32_t length_prefix = htonl(1);      // 4 length bytes
    memcpy(message, &length_prefix, 4);
    message[4] = INTERESTED;                // id byte

    if (send_message(peer, message, 5) == -1) {
        if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Failed to interest\n");
        return -1;
    }

    peer->is_interesting = true;            // By sending this message, we want to download from this peer
    return 0;
}

// Send not interested message to peer
int peer_manager_send_not_interested(Peer *peer) {
    uint8_t message[5];
    uint32_t length_prefix = htonl(1);      // 4 length bytes
    memcpy(message, &length_prefix, 4);
    message[4] = NOT_INTERESTED;            // id byte

    if (send_message(peer, message, 5) == -1) {
        if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Failed to not interest\n");
        return -1;
    }

    peer->is_interesting = false;           // By sending this message, we don't want to download from this peer
    return 0;
}

// Choke and send choke message to peer
int peer_manager_choke_peer(Peer *peer) {
    uint8_t message[5];
    uint32_t length_prefix = htonl(1);      // 4 length bytes
    memcpy(message, &length_prefix, 4);
    message[4] = CHOKE;                     // id byte

    if (send_message(peer, message, 5) == -1) {
        if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Failed to choke\n");
        return -1;
    }

    peer->choking = true;
    return 0;
}

// Unchoke and send unchoke message to peer
int peer_manager_unchoke_peer(Peer *peer) {
    uint8_t message[5];
    uint32_t length_prefix = htonl(1);      // 4 length bytes
    memcpy(message, &length_prefix, 4);
    message[4] = UNCHOKE;                   // id byte

    if (send_message(peer, message, 5) == -1) {
        if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Failed to choke\n");
        return -1;
    }

    peer->choking = false;
    return 0;
}

// Send bitfield to peer
int send_bitfield(Peer *peer) {
    size_t bitfield_bytes = (peer->bitfield_bits + 7) / 8;
    uint8_t message[5 + peer->bitfield_bits];

    uint32_t length_prefix = htonl(1 + bitfield_bytes);     // 4 length bytes
    memcpy(message, &length_prefix, 4);
    message[4] = BITFIELD;                                  // id byte

    memcpy(message + 5, peer->bitfield, bitfield_bytes);

    if (send_message(peer, message, 5 + peer->bitfield_bits) == -1) {
        if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Failed to send bitfield\n");
        return -1;
    }

    return 0;
}

// Sends the request and queues it as an outstanding request and will be stored for reference until a corresponding piece is received.
int peer_manager_send_request(Peer *peer, uint32_t request_index, uint32_t request_begin, uint32_t request_length) {
    if (peer->num_outstanding_requests >= MAX_OUTSTANDING_REQUESTS) {
        if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Too many outstanding requests for this peer, try again later\n");
        return -1;
    }

    // Build the request message
    int offset = 0;
    uint8_t message[17];
    uint32_t length_prefix = htonl(13);
    memcpy(message + offset, &length_prefix, 4);
    offset += 4;
    message[4] = REQUEST;
    offset++;
    uint32_t index = htonl(request_index);
    uint32_t begin = htonl(request_begin);
    uint32_t length = htonl(request_length);
    memcpy(message + offset, &index, 4);
    offset += 4;
    memcpy(message + offset, &begin, 4);
    offset += 4;
    memcpy(message + offset, &length, 4);
    offset += 4;

    if (offset != 17) {
        if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Something went wrong while constructing the request message\n");
        return -1;
    }

    if (send_message(peer, message, 17) == -1) {
        if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Something went wrong while sending the request message\n");
        return -1;
    }

    // Enqueue the request message in the outstanding_requests circular array
    peer->outstanding_requests[peer->requests_tail].index = request_index;
    peer->outstanding_requests[peer->requests_tail].begin = request_begin;
    peer->outstanding_requests[peer->requests_tail].length = request_length;
    peer->requests_tail = (peer->requests_tail + 1) % MAX_OUTSTANDING_REQUESTS;
    peer->num_outstanding_requests++;

    return 0;
}

// Send a keepalive message to peer
int peer_manager_send_keepalive_message(Peer *peer) {
    uint8_t message[4];
    uint32_t length_prefix = htonl(0);      // 4 length bytes
    memcpy(message, &length_prefix, 4);

    if (send_message(peer, message, 4) == -1) {
        if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Failed to send keepalive message\n");
        return -1;
    }
    return 0;
}

// Receive incoming, store in buffer, and process
int peer_manager_receive_messages(Peer *peer) {
    int received = recv(
        peer->sock_fd,
        peer->incoming_buffer + peer->incoming_buffer_offset,
        MAX_INCOMING_BYTES - peer->incoming_buffer_offset,
        MSG_DONTWAIT
    );
    if (received == 0) {
        if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: receive_messages failed, peer has disconnected\n");
        return 0;
    }
    if (received == -1) {
        if (get_args().debug_mode) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr, "[PEER_MANAGER]: receive_messages failed, nothing to receive (yet)\n");
            } else {
                fprintf(stderr, "[PEER_MANAGER]: receive_messages failed, something went wrong while trying to receive a message\n");
            }
        }
        return -1;
    }

    peer->incoming_buffer_offset += received;
    peer->bytes_recv += received;
    parse_peer_incoming_buffer(peer);
    return received;
}

// Add and connect to a new peer, sending it a handshake
int peer_manager_add_peer(Torrent torrent, const struct sockaddr_in *addr, socklen_t addr_len) {
    int new_sock;
    struct sockaddr_in new_addr;
    socklen_t addr_size = sizeof(new_addr);
    memset(&new_addr, 0, sizeof(new_addr));   // Initialize

    struct pollfd *fds = get_fds();
    Peer *peers = get_peers();
    int *num_fds = get_num_fds();
    int *num_peers = get_num_peers();

    new_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (new_sock == -1) {
        if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Failed to get new socket while creating new connection with %s\n", inet_ntoa(addr->sin_addr));
        return -1;
    }

    if (addr == NULL) {
        // Check if any new peers are connecting
        if (fds[0].revents & POLLIN) {
            new_sock = accept(fds[0].fd, (struct sockaddr *)&new_addr, &addr_size);
            if (new_sock == -1) {
                return -1;
            }
        }
    } else {
        if (connect(new_sock, (struct sockaddr *)addr, addr_len) == -1) {
            if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Failed to create connection with %s\n", inet_ntoa(addr->sin_addr));
        }
    }

    // Add the new peers socket to the pollfd array and peers array
    fds[*num_fds].fd = new_sock;
    fds[*num_fds].events = POLLIN;
    num_fds++;
    
    // Initializing all the fields for the peers array
    peers[*num_peers].bitfield = NULL;      // We can expect this to be initialized later
    peers[*num_peers].bitfield_bits = 0;
    // incoming_buffer doesn't need assignment
    peers[*num_peers].incoming_buffer_offset = 0;
    peers[*num_peers].torrent = torrent;
    peers[*num_peers].sock_fd = new_sock;
    if (addr == NULL) {
        peers[*num_peers].address = new_addr.sin_addr.s_addr;
        peers[*num_peers].port = new_addr.sin_port;
    } else {
        peers[*num_peers].address = addr->sin_addr.s_addr;
        peers[*num_peers].port = addr->sin_port;
    }
    // don't assign id until handshake is received
    ssize_t bytes_sent = 0;
    ssize_t bytes_recv = 0;
    gettimeofday(&peers[*num_peers].last_rate_time, NULL);
    double upload_rate = 0;
    double download_rate = 0;
    peers[*num_peers].num_outstanding_requests = 0;
    peers[*num_peers].requests_tail = 0;
    peers[*num_peers].requests_head = 0;
    // outstanding_requests doesn't need assignment
    peers[*num_peers].choking = true;
    peers[*num_peers].is_interesting = false;
    peers[*num_peers].choked = true;
    peers[*num_peers].is_interested = false;

    (*num_peers)++;
    
    if (get_args().debug_mode) {
        char addr_str[INET_ADDRSTRLEN];
        struct in_addr addr;
        addr.s_addr = peers[(*num_peers) - 1].address;
        inet_ntop(AF_INET, &addr, addr_str, sizeof(addr_str));
        fprintf(stderr, "[PEER_MANAGER]: New peer from %s on socket %d\n", addr_str, new_sock);
    }

    // Send handshake immediately after connection is made
    send_handshake(&peers[(*num_peers) - 1]);

    return new_sock;
}

// Disconnect and remove a specified peer. Compacts the fds and peers arrays by filling the resulting empty hole when the peer is removed.
int peer_manager_remove_peer(Peer *peer) {
    struct pollfd *fds = get_fds();
    Peer *peers = get_peers();
    int *num_fds = get_num_fds();
    int *num_peers = get_num_peers();

    int fds_index = -1;
    for (int i = 0; i < *num_peers; i++) {
        if (peer->id == peers[i].id) {
            fds_index = i + 1;
            break;
        }
    }

    if (fds_index == -1) {
        if (get_args().debug_mode) fprintf(stderr, "[PEER_MANAGER]: Attempted to remove a peer that did not exist\n");
        return -1;
    }

    int peer_index = fds_index - 1;

    // Disconnecting the peer
    int old_fd = fds[fds_index].fd;
    close(old_fd);

    uint32_t old_address = peers[peer_index].address;

    // The entry is now empty, so compact the fds and clients array (fill in the empty space)
    (*num_fds)--;
    fds[fds_index] = fds[*num_fds];
    (*num_peers)--;

    // Freeing any fields, if needed, since they will be replaced (we don't want memory leaks)
    if (peers[peer_index].bitfield != NULL) {
        free(peers[(fds_index) - 1].bitfield);
    }
    peers[peer_index] = peers[*num_peers];

    if (get_args().debug_mode) {
        char addr_str[INET_ADDRSTRLEN];
        struct in_addr addr;
        addr.s_addr = old_address;
        inet_ntop(AF_INET, &addr, addr_str, sizeof(addr_str));
        fprintf(stderr, "[PEER_MANAGER]: Removed peer from %s of socket %d\n", addr_str, old_fd);
    }

    return 0;
}