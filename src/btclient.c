#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <time.h>

#include "btclient.h"
#include "torrent_parser.h"
#include "peer_manager.h"
#include "tracker.h"
#include "piece_manager.h"

// Useful ANSI codes (source: https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797)
#define CLEAR_SCREEN "\033[2J\033[H"    // Erase screen, move cursor to home position (0, 0)
#define UNDERLINE "\x1b[4;58;5;15m"              // Underline text
#define RESET_UNDERLINE "\x1b[0m"       // Cancel underlining of texts
#define RESET_TEXT "\x1b[39m"           // Set text to default color (white)
#define GREEN_TEXT "\x1b[32m"           // Set text to green
#define BLUE_TEXT "\x1b[34m"            // Set text to blue
#define CYAN_TEXT "\x1b[36m"            // Set text to cyan

static Peer peers[MAX_PEERS];
static struct pollfd fds[MAX_PEERS + 1];
static int num_fds;
static int num_peers;

static struct run_arguments args;
static Torrent *current_torrent = NULL;
static unsigned char client_peer_id[20];

struct run_arguments get_args(void) { 
    return args; 
}

struct pollfd *get_fds(void) { 
    return fds; 
}

int *get_num_fds(void) {
    return &num_fds;
}

Peer *get_peers(void) {
    return peers; 
}

int *get_num_peers(void) {
    return &num_peers;
}

// Prints progress bar (progress must be between 0 and 1)
static void print_progress_bar(double progress) {
    int bar_width = 150;

    if (progress < 0) progress = 0;
    if (progress > 1) progress = 1;
    int offset = bar_width * progress;

    printf("|");
    for (int i = 0; i < bar_width; ++i) {
        if (i < offset) {
            printf("%s%s█%s%s", UNDERLINE, BLUE_TEXT, RESET_UNDERLINE, RESET_TEXT);
        } else if (i == offset) {
            printf("%s%s▒%s%s", UNDERLINE, BLUE_TEXT, RESET_UNDERLINE, RESET_TEXT);
        } else {
            printf("%s%s░%s%s", UNDERLINE, BLUE_TEXT, RESET_UNDERLINE, RESET_TEXT);
        }
    }
    printf("| %.2f%%\r", progress * 100.0);
    fflush(stdout);
}

int client_listen(int port) {
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        if (get_args().debug_mode) {
            fprintf(stderr, "[BTCLIENT_LISTEN]: Socket creation failed: %s\n", strerror(errno)); 
            fflush(stderr);
        }
        return -1;
    }
    if (get_args().debug_mode) { 
        fprintf(stderr, "[BTCLIENT_LISTEN]: Listener socket created: %d\n", listen_sock); 
        fflush(stderr); 
    }

    int flags = fcntl(listen_sock, F_GETFL, 0);
    if (flags == -1) {
        if (get_args().debug_mode) {
            fprintf(stderr, "[BTCLIENT_LISTEN]: Failed to get flags for listen socket: %s\n", strerror(errno)); 
            fflush(stderr);
        }
        close(listen_sock);
        return -1;
    }
    if (fcntl(listen_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        if (get_args().debug_mode) {
            fprintf(stderr, "[BTCLIENT_LISTEN]: Failed to set listen socket to non-blocking: %s\n", strerror(errno)); 
            fflush(stderr);
        }
        close(listen_sock);
        return -1;
    }
    if (get_args().debug_mode) { 
        fprintf(stderr, "[BTCLIENT_LISTEN]: Listener socket set to non-blocking.\n"); 
        fflush(stderr); 
    }

    fds[0].fd = listen_sock;
    fds[0].events = POLLIN;
    num_fds = 1;
    num_peers = 0;

    int toggle = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &toggle, sizeof(toggle)) == -1) {
        if (get_args().debug_mode) { 
            fprintf(stderr, "[BTCLIENT_LISTEN]: setsockopt SO_REUSEADDR failed: %s\n", strerror(errno)); 
            fflush(stderr); 
        }
    } else {
        if (get_args().debug_mode) { 
            fprintf(stderr, "[BTCLIENT_LISTEN]: setsockopt SO_REUSEADDR set.\n"); fflush(stderr); 
        }
    }
    if (setsockopt(listen_sock, IPPROTO_TCP, TCP_NODELAY, &toggle, sizeof(toggle)) == -1) {
        if (get_args().debug_mode) { 
            fprintf(stderr, "[BTCLIENT_LISTEN]: setsockopt TCP_NODELAY failed: %s\n", strerror(errno)); 
            fflush(stderr); 
        }
    } else {
        if (get_args().debug_mode) { 
            fprintf(stderr, "[BTCLIENT_LISTEN]: setsockopt TCP_NODELAY set.\n"); 
            fflush(stderr); 
        }
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        if (get_args().debug_mode) {
            fprintf(stderr, "[BTCLIENT_LISTEN]: Bind failed for port %d: %s\n", port, strerror(errno)); 
            fflush(stderr);
        }
        close(listen_sock);
        return -1;
    }
    if (get_args().debug_mode) { 
        fprintf(stderr, "[BTCLIENT_LISTEN]: Socket bound to port %d.\n", port); 
        fflush(stderr); 
    }

    if (listen(listen_sock, MAX_PEERS) == -1) {
        if (get_args().debug_mode) {
            fprintf(stderr, "[BTCLIENT_LISTEN]: Listen failed: %s\n", strerror(errno)); 
            fflush(stderr);
        }
        close(listen_sock);
        return -1;
    }

    if (get_args().debug_mode) {
        fprintf(stderr, "[BTCLIENT_LISTEN]: Listening on port %d...\n", port); fflush(stderr);
    }
    return 0;
}

void connect_peers(int num_peers_from_tracker, TrackerResponse response) {
    if (num_peers_from_tracker > 0) {
        if (get_args().debug_mode) { 
            fprintf(stderr, "[BTCLIENT_MAIN]: Attempting to connect to %d peers from tracker...\n", num_peers_from_tracker); 
            fflush(stderr); 
        }
        for (int i = 0; i < num_peers_from_tracker; i++) {
            struct sockaddr_in peer_addr_sa;
            memset(&peer_addr_sa, 0, sizeof(peer_addr_sa));
            peer_addr_sa.sin_family = AF_INET;
            peer_addr_sa.sin_port = htons(response.peers[i].port);
            peer_addr_sa.sin_addr.s_addr = htonl(response.peers[i].address);

            char peer_ip_log_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(peer_addr_sa.sin_addr), peer_ip_log_str, INET_ADDRSTRLEN);
            if (get_args().debug_mode) { 
                fprintf(stderr, "[BTCLIENT_MAIN]: Attempting to add peer %s:%d\n", peer_ip_log_str, ntohs(peer_addr_sa.sin_port)); 
                fflush(stderr); 
            }

            int new_sock = peer_manager_add_peer(*current_torrent, &peer_addr_sa, sizeof(peer_addr_sa));
            if (new_sock > 0) {
                if (get_args().debug_mode) { 
                    fprintf(stderr, "[BTCLIENT_MAIN]: Initiated connection process for peer %s:%d. Peer socket: %d. Current num_fds: %d, num_peers: %d\n", 
                        peer_ip_log_str, ntohs(peer_addr_sa.sin_port), new_sock, *get_num_fds(), *get_num_peers()); 
                        fflush(stderr); 
                }
            } else {
                if (get_args().debug_mode) { 
                    fprintf(stderr, "[BTCLIENT_MAIN]: Failed to add peer %s:%d.\n", peer_ip_log_str, ntohs(peer_addr_sa.sin_port)); 
                    fflush(stderr); 
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    printf(CLEAR_SCREEN);        // Clear the terminal screen for torrent info and progress bar
    args = arg_parseopt(argc, argv);

    if (args.debug_mode) {
        if (freopen("debug.log", "w", stderr) == NULL) {
            // If freopen fails, stderr is still console. Further fprintf(stderr,...) will go to console.
            fprintf(stdout, "CRITICAL: Failed to redirect stderr to debug.log. Debug messages will go to console if possible.\n");
            // We might not want to exit here, but continue with stderr as console for debugging.
        }
        fprintf(stderr, "[BTCLIENT_MAIN]: Debug mode enabled. Log output target: debug.log (or console if redirection failed).\n");
        fflush(stderr);
    }

    const char *filename = args.filename;
    if (!filename) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Error: No torrent file specified. Use -f <filename>\n"); 
        fflush(stderr);
        exit(1);
    }

    // TODO: clean up size being allocated for the buffer (line 185 too)
    char *buffer = malloc(1024 * 1024 * 1024);
    if (!buffer) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Error: Failed to allocate buffer for torrent file.\n"); fflush(stderr);
        exit(1);
    }

    int bytes_read = read_torrent_file(filename, buffer, 1024 * 1024 * 1024);
    if (bytes_read <= 0) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Error: Could not read torrent file '%s'. Bytes read: %d\n", filename, bytes_read); 
        fflush(stderr);
        free(buffer);
        exit(1);
    }

    // current_torrent is Torrent*
    printf(GREEN_TEXT);
    if (parse_torrent_file(buffer, bytes_read, &current_torrent) != 0 || !current_torrent) {
        printf(RESET_TEXT);
        fprintf(stderr, "[BTCLIENT_MAIN]: Error: Could not parse torrent file '%s'.\n", filename); 
        fflush(stderr);
        free(buffer);
        exit(1);
    }
    printf(RESET_TEXT);
    // TODO: delete unnecessary print statements later
    // torrent_parser.c's printf output to stdout happens inside parse_torrent_file
    if (get_args().debug_mode) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Torrent file parsed successfully (by torrent_parser module).\n");
        fflush(stderr);
        fprintf(stderr, "[BTCLIENT_MAIN]: Announce URL from parsed torrent: %s\n", current_torrent->announce);
        fflush(stderr);
    }

    free(buffer);

    long total_len;
    if (current_torrent->info.mode_type == MODE_SINGLE_FILE) {
        total_len = current_torrent->info.mode.single_file.length;
    } else {
        total_len = current_torrent->info.mode.multi_file.total_length;
    }

    // TODO: clean up how we're setting peer id 
    srand(time(NULL));
    snprintf((char*)client_peer_id, sizeof(client_peer_id), "-PC0123-%011ld", rand() % 100000000000L);

    TrackerResponse response = tracker_get(current_torrent->announce, current_torrent->info_hash, client_peer_id, args.port, 0, 0, total_len);
    if (get_args().debug_mode) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Tracker Response -> Interval: %d, Complete: %d, Incomplete: %d, Num Peers: %d\n",
                response.interval, response.complete, response.incomplete, response.num_peers);
        fflush(stderr);
    }

    int num_peers_from_tracker = response.num_peers;
    
    if (client_listen(args.port) != 0) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Error: Failed to start listening on port %d.\n", args.port); 
        fflush(stderr);
        free_tracker_response(&response);
        torrent_free(current_torrent);
        exit(1);
    }
    
    const char *output_filename = current_torrent->info.name ? current_torrent->info.name : "downloaded_file";
    if (piece_manager_init(current_torrent, output_filename) != 0) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Error: Failed to initialize piece manager.\n"); 
        fflush(stderr);
        free_tracker_response(&response);
        torrent_free(current_torrent);
        exit(1);
    }

    connect_peers(num_peers_from_tracker, response);

    if (get_args().debug_mode) {
        fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Entering main poll loop. Initial num_fds: %d, num_peers: %d\n", *get_num_fds(), *get_num_peers());
        fflush(stderr);
    }

    printf("\n%s DOWNLOAD PROGRESS%s\n", BLUE_TEXT, RESET_TEXT);
    print_progress_bar(total_len > 0 ? (double)piece_manager_get_bytes_downloaded_total() / total_len : 0.0);

    // infinite loop
    connect_peers(num_peers_from_tracker, response);
    while (1) {
        if (get_args().debug_mode && (*get_num_fds() > 1 || *get_num_peers() > 0) ) {
             fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Polling %d FDs. Active Peers: %d. Downloaded: %lu / %ld (%.2f%%)\n",
                    *get_num_fds(), *get_num_peers(), piece_manager_get_bytes_downloaded_total(), total_len, 
                    total_len > 0 ? (double)piece_manager_get_bytes_downloaded_total() * 100.0 / total_len : 0.0);
             fflush(stderr);
        }

        int poll_timeout_ms = 1000;
        int poll_result = poll(fds, *get_num_fds(), poll_timeout_ms);

        if (poll_result == -1) {
            if (errno == EINTR) {
                if (get_args().debug_mode) { 
                    fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Poll interrupted by signal, retrying.\n"); 
                    fflush(stderr); 
                }
                continue;
            }
            if (get_args().debug_mode) {
                fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Poll failed: %s. Exiting loop.\n", strerror(errno)); 
                fflush(stderr);
            }
            break;
        }

        if (poll_result == 0 && get_args().debug_mode && (*get_num_fds() <=1 && *get_num_peers() == 0)) {
            fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Poll timed out. No events. No connected peers. Num_fds: %d\n", *get_num_fds()); 
            fflush(stderr);
        }

        if (fds[0].revents & POLLIN) {
            if (get_args().debug_mode) { 
                fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Incoming connection detected on listen socket %d.\n", fds[0].fd); 
                fflush(stderr); 
            }
            peer_manager_add_peer(*current_torrent, NULL, 0);
        }
        
        for (int i = 1; i < *get_num_fds(); ) {
            Peer *current_peer = &peers[i - 1];
            int peer_log_idx = i - 1; // For logging

            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                if (get_args().debug_mode) {
                    fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Peer at fds[%d] (socket %d, peer_idx %d) has error/hup/nval event (0x%x). Removing peer.\n", 
                        i, fds[i].fd, peer_log_idx, fds[i].revents); fflush(stderr);
                }
                peer_manager_remove_peer(current_peer);
                continue;
            }

            if (fds[i].revents & POLLOUT) {
                // Ensure current_peer points to a valid peer after potential removals
                // This check might be redundant if continue is used above, but for safety:
                if (i >= *get_num_fds()) {
                    break; // Array bounds changed
                } 
                current_peer = &peers[i - 1]; // Re-fetch, peer_manager_remove_peer might compact

                if (!current_peer->handshake_done) {
                    int sock_err = 0;
                    socklen_t len = sizeof(sock_err);
                    if (getsockopt(fds[i].fd, SOL_SOCKET, SO_ERROR, &sock_err, &len) == -1 || sock_err != 0) {
                        if (get_args().debug_mode) {
                            fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Outgoing connection for peer_idx %d (socket %d) failed: %s. Removing.\n", 
                                peer_log_idx, fds[i].fd, sock_err == 0 ? "Unknown err" : strerror(sock_err)); 
                            fflush(stderr);
                        }
                        peer_manager_remove_peer(current_peer);
                        continue;
                    } else {
                        fds[i].events &= ~POLLOUT;
                        fds[i].events |= POLLIN;
                        if (get_args().debug_mode) {
                            fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Outgoing connection for peer_idx %d (socket %d) successful.\n", 
                                peer_log_idx, fds[i].fd); 
                            fflush(stderr);
                        }
                    }
                } else {
                    if (get_args().debug_mode) { 
                        fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Socket %d for peer_idx %d writable (POLLOUT).\n", 
                            fds[i].fd, peer_log_idx); 
                        fflush(stderr); 
                    }
                }
            }

            if (fds[i].revents & POLLIN) {
                if (i >= *get_num_fds()) {
                    break;
                }
                current_peer = &peers[i - 1];

                if (get_args().debug_mode) { 
                    fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Data available from peer_idx %d (socket %d).\n", 
                        peer_log_idx, fds[i].fd); 
                    fflush(stderr); 
                }
                int receive_status = peer_manager_receive_messages(current_peer);
                if (receive_status == 0) {
                    if (get_args().debug_mode) { 
                        fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Peer_idx %d (socket %d) disconnected or error in receive. Removing.\n", 
                            peer_log_idx, fds[i].fd); 
                        fflush(stderr); 
                    }
                    peer_manager_remove_peer(current_peer);
                    continue;
                }
                print_progress_bar(total_len > 0 ? (double)piece_manager_get_bytes_downloaded_total() / total_len : 0.0);
            }
            
            // Re-fetch current_peer pointer in case peer_manager_remove_peer was called by handlers above for a *different* peer, changing array
            // This is complex; safer if peer_manager_remove_peer is only called if `current_peer` is the one being removed.
            // The `continue` statements after removal should handle this loop's index correctly.
            if (i >= *get_num_fds()) { 
                break; // Check bounds again before member access
            }
            current_peer = &peers[i - 1];

            if (current_peer->handshake_done && !current_peer->choked && current_peer->is_interesting && current_peer->bitfield != NULL) {
                while (current_peer->num_outstanding_requests < MAX_OUTSTANDING_REQUESTS) {
                    uint32_t block_begin_offset;
                    uint32_t block_length;
                    bool found_piece_to_request_this_iteration = false;

                    uint32_t total_pieces_in_torrent = piece_manager_get_total_pieces_count();
                    for (uint32_t p_idx = 0; p_idx < total_pieces_in_torrent; ++p_idx) {
                        if (piece_manager_get_piece_state(p_idx) == PIECE_STATE_MISSING || piece_manager_get_piece_state(p_idx) == PIECE_STATE_PENDING) {
                            bool peer_has_this_piece = false;
                            if (current_peer->bitfield && (p_idx / 8) < current_peer->bitfield_bytes) {
                                peer_has_this_piece = (current_peer->bitfield[p_idx / 8] >> (7 - (p_idx % 8))) & 1;
                            }

                            if (peer_has_this_piece) {
                                if (piece_manager_get_block_to_request_from_piece(p_idx, &block_begin_offset, &block_length)) {
                                    if (get_args().debug_mode) { 
                                        fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Requesting from peer_idx %d: Piece %u, Offset %u, Length %u\n", 
                                            peer_log_idx, p_idx, block_begin_offset, block_length); 
                                            fflush(stderr); 
                                    }
                                    if (peer_manager_send_request(current_peer, p_idx, block_begin_offset, block_length) != 0) {
                                        if (get_args().debug_mode) { fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Failed to send REQUEST to peer_idx %d for Piece %u.\n", 
                                            peer_log_idx, p_idx); 
                                            fflush(stderr); 
                                        }
                                    }
                                    found_piece_to_request_this_iteration = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (!found_piece_to_request_this_iteration) {
                        break;
                    }  
                }
            } else if (current_peer->handshake_done && current_peer->bitfield != NULL && !current_peer->is_interesting) {
                // Simplified interest check (should ideally be in peer_manager or more robust)
                uint32_t total_pieces_in_torrent = piece_manager_get_total_pieces_count();
                for (uint32_t p_idx = 0; p_idx < total_pieces_in_torrent; ++p_idx) {
                    if (piece_manager_get_piece_state(p_idx) != PIECE_STATE_HAVE) {
                        bool peer_has_this_piece = false;
                        if (current_peer->bitfield && (p_idx / 8) < current_peer->bitfield_bytes) {
                            peer_has_this_piece = (current_peer->bitfield[p_idx / 8] >> (7 - (p_idx % 8))) & 1;
                        }
                        if (peer_has_this_piece) {
                            if (get_args().debug_mode) { fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Peer_idx %d has pieces we need. Sending INTERESTED.\n", peer_log_idx); fflush(stderr); }
                            peer_manager_send_interested(current_peer); // This should set current_peer->is_interesting = true
                            break; 
                        }
                    }
                }
            }
            i++;
        }

        if (piece_manager_is_download_complete()) {
            fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: ****** Download complete! Output file: %s ******\n", output_filename); 
            fflush(stderr);
            break;
        }
    }

    free_tracker_response(&response);
    if (get_args().debug_mode) { 
        fprintf(stderr, "[BTCLIENT_MAIN]: Freed tracker response data.\n"); 
        fflush(stderr); 
    }

    printf("\n");           // Exit progress bar

    if (get_args().debug_mode) { 
        fprintf(stderr, "[BTCLIENT_MAIN]: Exited main poll loop.\n"); fflush(stderr); 
    }

    if (get_args().debug_mode) { 
        fprintf(stderr, "[BTCLIENT_MAIN]: Cleaning up peer connections...\n"); 
        fflush(stderr); 
    }
    for (int i_cleanup = (*get_num_fds()) - 1; i_cleanup >= 1; i_cleanup--) {
        if (i_cleanup -1 < *get_num_peers() && i_cleanup -1 >= 0) {
            Peer *peer_to_remove = &peers[i_cleanup-1];
            if (get_args().debug_mode) { 
                fprintf(stderr, "[BTCLIENT_MAIN]: Removing peer at fds index %d (socket %d, peer_idx %d) during final cleanup.\n", 
                    i_cleanup, fds[i_cleanup].fd, (i_cleanup-1)); 
                fflush(stderr); }
            peer_manager_remove_peer(peer_to_remove);
        } else {
            if (get_args().debug_mode && fds[i_cleanup].fd != -1) { 
                fprintf(stderr, "[BTCLIENT_MAIN]: Discrepancy or already closed fd at fds index %d during cleanup. num_peers: %d, fd: %d\n", 
                    i_cleanup, *get_num_peers(), fds[i_cleanup].fd); 
                fflush(stderr); 
            }
            if(fds[i_cleanup].fd != -1) {
                close(fds[i_cleanup].fd);
                fds[i_cleanup].fd = -1;
            }
        }
    }
    if (get_args().debug_mode) { 
        fprintf(stderr, "[BTCLIENT_MAIN]: Finished peer cleanup. Num_fds: %d, Num_peers: %d\n", *get_num_fds(),*get_num_peers());
        fflush(stderr); 
    }

    piece_manager_destroy();
    if (get_args().debug_mode) { 
        fprintf(stderr, "[BTCLIENT_MAIN]: Piece manager destroyed.\n"); 
        fflush(stderr); 
    }

    torrent_free(current_torrent);
    current_torrent = NULL;
    if (get_args().debug_mode) { 
        fprintf(stderr, "[BTCLIENT_MAIN]: Torrent data freed.\n"); 
        fflush(stderr); 
    }

    if (fds[0].fd != -1) {
        if (get_args().debug_mode) { 
            fprintf(stderr, "[BTCLIENT_MAIN]: Closing listen socket %d.\n", fds[0].fd); 
            fflush(stderr); 
        }
        close(fds[0].fd);
        fds[0].fd = -1;
    }
    *get_num_fds() = 0;
    *get_num_peers() = 0;

    if (get_args().debug_mode) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Client shutting down.\n");
        fflush(stderr); 
    }
    return 0;
}