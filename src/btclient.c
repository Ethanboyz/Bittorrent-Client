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
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "arg_parser.h"
#include "torrent_parser.h"
#include "peer_manager.h"
#include "tracker.h"
#include "piece_manager.h" // MOD: Include piece manager

// Keep track of connected peers (and their socket fds)
// fds[0] is always the listen socket
static struct pollfd fds[MAX_PEERS + 1];
static Peer peers[MAX_PEERS];
static int num_fds;
static int num_peers;

static struct run_arguments args;
static Torrent *current_torrent = NULL; // MOD: Store the parsed torrent globally

// Return run arguments
struct run_arguments get_args(void) { return args; }

// Getters
struct pollfd *get_fds(void) { return fds; } // Return "authentic" fds array
int *get_num_fds(void)
{
    return &num_fds;
}
// Return number of fds in fds array

Peer *
get_peers(void)
{
    return peers;
}
// Return "authentic" peers array
int *get_num_peers(void)
{
    return &num_peers;
} // Return number of peers

// Start client listening for incoming connections
int client_listen(int port)
{
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    // Initialize

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    // Create new TCP server socket
    if (listen_sock == -1)
    {
        if (get_args().debug_mode)
            fprintf(stderr, "Socket creation failed: %s\n", strerror(errno));
        exit(1);
    }

    // MOD: Set listen socket to non-blocking
    int flags = fcntl(listen_sock, F_GETFL, 0);
    if (fcntl(listen_sock, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        if (get_args().debug_mode)
            fprintf(stderr, "Failed to set listen socket to non-blocking: %s\n", strerror(errno));
        close(listen_sock);
        exit(1);
    }

    // Initialize fds array
    fds[0].fd = listen_sock;
    fds[0].events = POLLIN; // Poll for incoming connections
    num_fds = 1;
    num_peers = 0;

    int toggle = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &toggle, sizeof(toggle)); // Prevent slowdowns in testing by allowing quick reuse of IP and port
    setsockopt(listen_sock, IPPROTO_TCP, TCP_NODELAY, &toggle, sizeof(toggle)); // Prevent slowdowns in sending (especially handshake) by disabling Nagle's Algorithm

    // Have the server listen in from anywhere on the specified port
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Binding the server socket and setting it to listen
    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        if (get_args().debug_mode)
            fprintf(stderr, "Bind failed: %s\n", strerror(errno));
        close(listen_sock); // MOD: Close socket on failure
        return -1;
    }
    if (listen(listen_sock, 32) == -1)
    {
        if (get_args().debug_mode)
            fprintf(stderr, "Listen failed: %s\n", strerror(errno));
        close(listen_sock); // MOD: Close socket on failure
        return -1;
    }

    if (get_args().debug_mode)
        fprintf(stderr, "Listening on port %d...\n", port);
    return 0;
}

int main(int argc, char *argv[])
{
    args = arg_parseopt(argc, argv); // MOD: Assign to static args

    // Enable debug mode by directing stderr to log file
    if (args.debug_mode)
    {
        if (freopen("debug.log", "w", stderr) == NULL)
        {
            fprintf(stderr, "Failed to enable debug mode\n");
            exit(1);
        }
        fprintf(stderr, "Debug mode enabled. Log file: debug.log\n"); // MOD: Log that debug is on
    }

    // MOD: --- Torrent Parsing and Tracker Request ---
    const char *filename = args.filename;
    if (!filename)
    { // MOD: Ensure filename was provided
        fprintf(stderr, "Error: No torrent file specified. Use -f <filename>\n");
        exit(1);
    }

    char *buffer = malloc(1024 * 1024 * 1024); // MOD: Still large buffer
    if (!buffer)
    { // MOD: Check malloc
        fprintf(stderr, "Error: Failed to allocate buffer for torrent file.\n");
        exit(1);
    }

    int bytes_read = read_torrent_file(filename, buffer, 1024 * 1024 * 1024);
    if (bytes_read <= 0)
    { // MOD: Check for read errors
        fprintf(stderr, "Error: Could not read torrent file '%s'.\n", filename);
        free(buffer);
        exit(1);
    }
    printf("Successfully read %d bytes from %s\n\n", bytes_read, filename);

    Torrent *torrent = NULL;
    if (parse_torrent_file(buffer, bytes_read, &torrent) != 0 || !torrent)
    { // MOD: Check parse result
        fprintf(stderr, "Error: Could not parse torrent file '%s'.\n", filename);
        free(buffer);
        exit(1);
    }
    current_torrent = torrent; // MOD: Store the parsed torrent globally

    free(buffer); // MOD: Free buffer after parsing

    long total_len; // MOD: Use long
    if (torrent->info.mode_type == MODE_SINGLE_FILE)
    {
        total_len = torrent->info.mode.single_file.length;
    }
    else
    {
        total_len = torrent->info.mode.multi_file.total_length;
    }

    // TODO: Generate a unique peer ID for this client
    unsigned char client_peer_id[20] = "-PC0001-ABCDEFG12345"; // Placeholder

    TrackerResponse response = http_get(torrent->announce, torrent->info_hash, client_peer_id, args.port, 0, 0, total_len); // MOD: Pass total_len as 'left' initially

    int num_peers_from_tracker = response.num_peers;                          // MOD: Store count
    printf("Tracker response contained %d peers.\n", num_peers_from_tracker); // MOD: Use stored count

    // MOD: ************************** Piece Manager Initialization *******************
    const char *output_filename = torrent->info.name ? torrent->info.name : "downloaded_file"; // Use torrent name or default
    if (piece_manager_init(current_torrent, output_filename) != 0)
    { // MOD: Init piece manager with pointer
        fprintf(stderr, "Error: Failed to initialize piece manager.\n");
        free_tracker_response(&response);
        torrent_free(current_torrent); // MOD: Free torrent on error
        exit(1);
    }

    // MOD: ******************** Connect to Peers from Tracker  ************************
    printf("Attempting to connect to %d peers...\n", num_peers_from_tracker); // MOD: Log
    for (int i = 0; i < num_peers_from_tracker; i++)
    {
        struct sockaddr_in peer_addr;
        memset(&peer_addr, 0, sizeof(peer_addr));
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(response.peers[i].port);
        peer_addr.sin_addr.s_addr = htonl(response.peers[i].address); // Address is in host byte order in struct, convert to network

        // Add the peer using peer_manager (initiates connection and sends handshake)
        // peer_manager_add_peer handles adding to fds/peers arrays internally
        int peer_index = peer_manager_add_peer(*current_torrent, &peer_addr, sizeof(peer_addr)); // MOD: Pass torrent pointer, use &peer_addr
        if (peer_index != -1)
        {
            // Successfully initiated connection (might still be in progress)
            fprintf(stderr, "Successfully initiated connection (might still be in progress)\n");
        }
        else
        {
            
            // Failed to add peer (e.g., socket error, max peers)
            if (get_args().debug_mode)
                fprintf(stderr, "Failed to add peer %s:%d\n", inet_ntoa(peer_addr.sin_addr), ntohs(peer_addr.sin_port));
        }
    }

    free_tracker_response(&response); // Free the tracker response after connecting attempts

    // Start listening for incoming connections and messages (requests)
    if (client_listen(args.port) != 0)
    { // MOD: Check listen result
        fprintf(stderr, "Error: Failed to start listening.\n");
        piece_manager_destroy();       // MOD: Clean up piece manager
        torrent_free(current_torrent); // MOD: Clean up torrent
        exit(1);
    }

    // MOD: ***************** Main Poll Loop *******************************
    if (get_args().debug_mode)
        fprintf(stderr, "Entering main poll loop...\n");

    while (1)
    {
        // TODO: Add periodic tracker updates (based on interval)
        // TODO: downloading logic (requesting blocks from peers)
        // TODO: choking/unchoking logic
        // TODO: keepalive messages (send regularly if no other messages sent)

        // Use a timeout in poll to allow for periodic tasks (like tracker updates, rate calculations)
        int poll_timeout_ms = 1000;                            // Poll with a 1-second timeout
        int poll_result = poll(fds, num_fds, poll_timeout_ms); // MOD: Use num_fds

        if (poll_result == -1)
        {
            if (errno == EINTR)
                continue; // Interrupted by signal, just retry poll
            if (get_args().debug_mode)
                fprintf(stderr, "Poll failed: %s\n", strerror(errno)); // MOD: Log error
            break;                                                     // Exit loop on fatal poll error
        }

        // Handle events on the listen socket (new incoming connections)
        if (fds[0].revents & POLLIN)
        {
            // Accept the incoming connection and add the peer
            // peer_manager_add_peer called with addr=NULL attempts to accept ONE connection
            peer_manager_add_peer(*current_torrent, NULL, 0); // MOD: Pass torrent pointer
            // No need to loop or check return here, just try to accept one per poll cycle
        }

        // Handle events on peer sockets (starting from index 1)
        for (int i = 1; i < num_fds; /* no increment here, done manually*/)
        {
            Peer *current_peer = &peers[i - 1]; // Peers array is 0-indexed, fds is 1-indexed

            // Check for connection complete on outgoing sockets
            if (fds[i].revents & POLLOUT)
            {
                if (!current_peer->handshake_done)
                { // Only process POLLOUT for connections not yet handshaked
                    // Check if connect is complete by getting socket error
                    int sock_err = 0;
                    socklen_t len = sizeof(sock_err);
                    if (getsockopt(fds[i].fd, SOL_SOCKET, SO_ERROR, &sock_err, &len) == -1 || sock_err != 0)
                    {
                        if (get_args().debug_mode)
                            fprintf(stderr, "[BTCLIENT]: Outgoing connection to %s:%d failed: %s. Removing peer.\n", inet_ntoa(*(struct in_addr *)&current_peer->address), ntohs(current_peer->port), sock_err == 0 ? "Unknown error" : strerror(sock_err));
                        peer_manager_remove_peer(current_peer);
                        // Array was compacted, stay at the same index
                        continue;
                    }
                    else
                    {
                        // Connection successful! Stop polling for POLLOUT
                        fds[i].events &= ~POLLOUT;
                        if (get_args().debug_mode)
                            fprintf(stderr, "[BTCLIENT]: Outgoing connection to %s:%d successful.\n", inet_ntoa(*(struct in_addr *)&current_peer->address), ntohs(current_peer->port));
                        // Handshake was already sent by peer_manager_add_peer
                    }
                }
            }

            // Handle incoming data from peers
            if (fds[i].revents & POLLIN)
            {
                // MOD: receive messages from peers, handle accordingly
                int receive_status = peer_manager_receive_messages(current_peer); // 1 = OK, 0 = Disconnected/Error
                if (receive_status == 0)
                {
                    // peer_manager_receive_messages indicated disconnect or fatal error
                    peer_manager_remove_peer(current_peer);
                    // Array was compacted, stay at the same index
                    continue; // MOD: Skip increment to re-check the element moved here
                }
            }
            // TODO: Check for POLLERR, POLLHUP, POLLNVAL on peer sockets and remove peer if set

            // MOD: Implement download logic - Request blocks if unchoked and interested and need pieces
            if (!current_peer->choked && current_peer->is_interesting && current_peer->handshake_done)
            { // Must be unchoked by peer, interested in peer, and handshake done
                // Request more blocks if we have less than MAX_OUTSTANDING_REQUESTS
                while (current_peer->num_outstanding_requests < MAX_OUTSTANDING_REQUESTS)
                {
                    uint32_t piece_idx, begin, length;
                    // Select a piece that the peer has and we need, then get an unrequested block from it
                    // TODO: Implement piece selection logic (idk like rarest first)
                    // below is temp a simplified approach: get *any* block from *any* piece the peer has and we need.

                    // This simple approach would iterate through all our needed pieces and find one the peer has.
                    // A better way is to select the piece *first* using piece_manager_select_piece_for_peer,
                    // then get a block from *that* piece.

                    // Simplified block requesting loop (replace with proper piece selection):
                    bool requested = false;
                    uint32_t total_pieces = piece_manager_get_total_pieces_count();
                    for (uint32_t p_idx = 0; p_idx < total_pieces; ++p_idx)
                    {
                        // Check if we need this piece and the peer has it
                        if (piece_manager_get_piece_state(p_idx) == PIECE_STATE_MISSING || piece_manager_get_piece_state(p_idx) == PIECE_STATE_PENDING)
                        {
                            // Check if peer has piece p_idx (need a helper function for peer bitfield check)
                            // bool peer_has_piece = check_peer_has_piece(current_peer, p_idx); // TODO: Implement this helper
                            // For now, using a direct check which is less modular:
                            bool peer_has_piece = false;
                            if (current_peer->bitfield && p_idx / 8 < current_peer->bitfield_bytes)
                            {
                                peer_has_piece = (current_peer->bitfield[p_idx / 8] >> (7 - (p_idx % 8))) & 1;
                            }

                            if (peer_has_piece)
                            {
                                // Get the next unrequested block from this piece
                                if (piece_manager_get_block_to_request_from_piece(p_idx, &begin, &length))
                                {
                                    // Found a block to request
                                    if (peer_manager_send_request(current_peer, p_idx, begin, length) == 0)
                                    {
                                        requested = true;
                                        // Mark block as requested in piece manager? Or handle re-requesting if it times out?
                                    }
                                }
                            }
                            if (requested)
                                break; // Found a piece and requested a block, break inner loop
                        }
                        if (!requested)
                            break; // No blocks to request from any piece from this peer
                    }
                }
                // MOD: Update peer rates periodically
                update_download_upload_rate(current_peer);

                // MOD: Manually increment index
                i++;
            }

            // MOD: Check if download is complete
            if (piece_manager_is_download_complete())
            {
                fprintf(stderr, "Download complete!\n"); // piece_manager also logs this, but good to have here too
                // TODO: Clean up connections gracefully (send stopped event to tracker, close sockets)
                break; // Exit main loop
            } else {
                fprintf(stderr, "Download NOT complete, due to who knows what!\n");
            }
        }

        
        // TODO: Clean up all active peer connections before destroying managers

        piece_manager_destroy();       // MOD: Destroy piece manager
        torrent_free(current_torrent); // MOD: Free torrent

        // TODO: Close listen socket
        // TODO: Free fds and peers arrays if they were dynamically allocated (they are static here)

        if (get_args().debug_mode)
            fclose(stderr); // MOD: Close the log file

        return 0;
    }
}