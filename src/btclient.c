/*
* Main function here so entry point into rest of our code
* Executable runs this
* Reads command line arguments needed for parsing torrent and connecting to tracker
* TODO: need to decide how to connect this to the rest of our pieces
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>

#include "arg_parser.h"
#include "torrent_parser.h"
#include "peer_manager.h"

// Keep track of connected peers (and their socket fds)
static struct pollfd fds[MAX_PEERS + 1];
static Peer peers[MAX_PEERS];
static int num_fds;
static int num_peers;

static struct run_arguments args;

// Return run arguments
struct run_arguments get_args(void) {return args;}

// Getters
struct pollfd *get_fds(void) {return fds;}      // Return "authentic" fds array
int get_num_fds(void) {return num_fds;}         // Return number of fds in fds array
Peer *get_peers(void) {return peers;}           // Return "authentic" peers array
int get_num_peers(void) {return num_peers;}     // Return number of peers

// Start client listening for incoming connections
int client_listen(int port) {
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));       // Initialize

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);  // Create new TCP server socket
    if (listen_sock == -1) {
        if (get_args().debug_mode) fprintf(stderr, "Socket creation failed: %s\n", strerror(errno));
        exit(1);
    }

    // Initialize fds array
    fds[0].fd = listen_sock;
    fds[0].events = POLLIN;
    num_fds = 1;
    num_peers = 0;

    int toggle = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &toggle, sizeof(toggle)); // Prevent slowdowns in testing by allowing quick reuse of IP and port
    setsockopt(listen_sock, IPPROTO_TCP, TCP_NODELAY, &toggle, sizeof(toggle)); // Prevent slowdowns in sending messages by disabling Nagle's Algorithm

    // Have the server listen in from anywhere on the specified port
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Binding the server socket and setting it to listen
    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        if (get_args().debug_mode) fprintf(stderr, "Bind failed: %s\n", strerror(errno));
        return -1;
    }
    if (listen(listen_sock, 32) == -1) {
        if (get_args().debug_mode) fprintf(stderr, "Listen failed: %s\n", strerror(errno));
        return -1;
    }

    if (get_args().debug_mode) fprintf(stderr, "Listening on port %d...\n", port);
    return 0;
}

int main(int argc, char *argv[]) {
    struct run_arguments args = arg_parseopt(argc, argv);

    // Enable debug mode by directing stderr to log file
    if (args.debug_mode) {
        if (freopen("debug.log", "w", stderr) == NULL) {
            fprintf(stderr, "Failed to enable debug mode\n");
            exit(1);
        }
    }

    // TODO: parse torrent file, do other stuff, idk

    // Start listening for incoming connections and messages (requests)
    client_listen(args.port);
    while (1) {
        int poll_result = poll(fds, num_fds, 0);
        if (poll_result == -1) {
            if (get_args().debug_mode) fprintf(stderr, "Poll failed\n");
            break;
        } else {        // The poll revealed a new message or connection
            // TODO: peer_manager.c functions will add new peers, if applicable
            // TODO: receive messages from peers, handle accordingly
        }
    }
}