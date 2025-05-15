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
#define UNDERLINE "\x1b[4;58;5;15m"     // Underline text
#define RESET_UNDERLINE "\x1b[0m"       // Cancel underlining of texts
#define RESET_TEXT "\x1b[39m"           // Set text to default color (white)
#define GREEN_TEXT "\x1b[32m"           // Set text to green
#define BLUE_TEXT "\x1b[34m"            // Set text to blue
#define CYAN_TEXT "\x1b[36m"            // Set text to cyan

static Peer peers[MAX_PEERS];
static struct pollfd fds[MAX_PEERS + 1];
static int num_fds;
static int num_peers;

static bool endgame = false;

static struct run_arguments args;
static Torrent *current_torrent = NULL;
static unsigned char client_peer_id[20];

// Tracker refresh state
static time_t last_tracker_request_time;
static int tracker_interval_seconds;

static time_t last_optimistic_unchoke_time = 0;
#define OPTIMISTIC_UNCHOKE_INTERVAL 30 

static time_t last_choke_time = 0;
#define CHOKING_INTERVAL 10
#define MAX_UNCHOKED_PEERS 4

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

bool get_endgame(void) {
    return endgame;
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

// run optimistic unchoke every 30 seconds as described in wiki
void optimistic_unchoke(void) {
    Peer *peers = get_peers();
    int *num_peers = get_num_peers();
    time_t current_time = time(NULL);
    
    if (current_time - last_optimistic_unchoke_time < OPTIMISTIC_UNCHOKE_INTERVAL) {
        return; 
    }
    
    if (get_args().debug_mode) {
        fprintf(stderr, "[BTCLIENT]: Performing optimistic unchoke\n");
        fflush(stderr);
    }
    
    int potential_unchoke[MAX_PEERS];
    int num_potential = 0;
    
    // get a list of peers that we are currently choking and is interested in us
    for (int i = 0; i < *num_peers; i++) {
        if (peers[i].choking && peers[i].is_interested) {
            potential_unchoke[num_potential] = i;
            num_potential++;
        }
    }

    // at any one time there is a single peer which is unchoked regardless of its upload rate
    if (num_potential > 0) {
        int random_index = rand() % num_potential;
        int peer_to_unchoke = potential_unchoke[random_index];
        
        if (get_args().debug_mode) {
            fprintf(stderr, "[BTCLIENT]: Optimistically unchoking peer at index %d\n", peer_to_unchoke);
            fflush(stderr);
        }
        
        peer_manager_unchoke_peer(&peers[peer_to_unchoke]);
    }
    
    last_optimistic_unchoke_time = current_time;
}

// tit-for-tat-ish algorithm described in wiki
// change choked peers every 10 seconds
void choke_peer(void) {
    Peer *peers = get_peers();
    int *num_peers = get_num_peers();
    time_t current_time = time(NULL);
    
    if (current_time - last_choke_time < CHOKING_INTERVAL) {
        return;
    }
    
    if (get_args().debug_mode) {
        fprintf(stderr, "[BTCLIENT]: Running choking algorithm\n");
        fflush(stderr);
    }
    
    for (int i = 0; i < *num_peers; i++) {
        update_download_upload_rate(&peers[i]);
    }
        
    // track peers that are interested and their upload/download rates
    int peer_indices[MAX_PEERS];
    double peer_rates[MAX_PEERS];

    bool is_seeding = piece_manager_is_download_complete();
    
    for (int i = 0; i < *num_peers; i++) {
        peer_indices[i] = i;
        // client has complete file so track peers' download rates
        if (is_seeding) {
            peer_rates[i] = get_download_rate(&peers[i]);
        } else {
            // track peers' upload rates
            peer_rates[i] = get_upload_rate(&peers[i]);   
        }
    }
        
    // sort all peers by highest to lowest upload/download rates
    for (int i = 0; i < *num_peers; i++) {
        for (int j = i + 1; j < *num_peers; j++) {
            if (peer_rates[j] > peer_rates[i]) {
                double temp_rate = peer_rates[i];
                peer_rates[i] = peer_rates[j];
                peer_rates[j] = temp_rate;
                
                int temp_index = peer_indices[i];
                peer_indices[i] = peer_indices[j];
                peer_indices[j] = temp_index;
            }
        }
    }

    // unchoking the four peers which have the best upload/download rate and are interested
    // these are now the downloaders
    int num_downloaders = 0;
    for (int i = 0; i < *num_peers && num_downloaders < MAX_UNCHOKED_PEERS; i++) {
        int peer_idx = peer_indices[i];
        
        if (peers[peer_idx].is_interested) {
            if (peers[peer_idx].choking) {
                peer_manager_unchoke_peer(&peers[peer_idx]);
            }
            num_downloaders++;
        }
    }

    // peers which have a better upload rate (as compared to the downloaders) 
    // but aren't interested get unchoked
    double min_unchoked_rate = 0.0;
    if (num_downloaders >= MAX_UNCHOKED_PEERS) {
        for (int i = 0; i < *num_peers; i++) {
            int peer_idx = peer_indices[i];
            if (peers[peer_idx].is_interested && !peers[peer_idx].choking) {
                min_unchoked_rate = peer_rates[i];
                break;
            }
        }
    }

    for (int i = 0; i < *num_peers; i++) {
        int peer_idx = peer_indices[i];
        if (!peers[peer_idx].is_interested) {
            if (peer_rates[i] > min_unchoked_rate || num_downloaders < MAX_UNCHOKED_PEERS) {
                if (peers[peer_idx].choking) {
                    peer_manager_unchoke_peer(&peers[peer_idx]);
                }
            } else {
                if (!peers[peer_idx].choking) {
                    peer_manager_choke_peer(&peers[peer_idx]);
                }
            }
        // if they (referring to prev comment) become interested, 
        // the downloader with the worst upload rate gets choked
        } else if (peers[peer_idx].is_interested && !peers[peer_idx].choking && 
                peer_rates[i] < min_unchoked_rate && num_downloaders >= MAX_UNCHOKED_PEERS) {
            peer_manager_choke_peer(&peers[peer_idx]);
        }
    }
    last_choke_time = current_time;
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
            fprintf(stderr, "[BTCLIENT_LISTEN]: setsockopt SO_REUSEADDR set.\n"); 
            fflush(stderr); 
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

// MODIFIED: connect_peers function
void connect_peers(int num_peers_to_connect, TrackerResponse response_arg) {
    if (num_peers_to_connect > 0) {
        if (get_args().debug_mode) {
            fprintf(stderr, "[BTCLIENT_CONNECT_PEERS]: Attempting to connect to %d peers from tracker list...\n", num_peers_to_connect);
            fflush(stderr);
        }
        for (int i = 0; i < num_peers_to_connect; i++) {
            if (*get_num_peers() >= MAX_PEERS) {
                if (get_args().debug_mode) {
                    fprintf(stderr, "[BTCLIENT_CONNECT_PEERS]: Reached MAX_PEERS limit (%d), not attempting more connections from this tracker list.\n", MAX_PEERS);
                    fflush(stderr);
                }
                break;
            }

            struct sockaddr_in peer_addr_sa;
            memset(&peer_addr_sa, 0, sizeof(peer_addr_sa));
            peer_addr_sa.sin_family = AF_INET;
            peer_addr_sa.sin_port = htons(response_arg.peers[i].port);
            peer_addr_sa.sin_addr.s_addr = htonl(response_arg.peers[i].address);

            char peer_ip_log_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(peer_addr_sa.sin_addr), peer_ip_log_str, INET_ADDRSTRLEN);
            if (get_args().debug_mode) {
                fprintf(stderr, "[BTCLIENT_CONNECT_PEERS]: Attempting to add peer %s:%d\n", peer_ip_log_str, ntohs(peer_addr_sa.sin_port));
                fflush(stderr);
            }

            int new_sock = peer_manager_add_peer(*current_torrent, &peer_addr_sa, sizeof(peer_addr_sa));
            if (new_sock > 0) {
                if (get_args().debug_mode) {
                    fprintf(stderr, "[BTCLIENT_CONNECT_PEERS]: Initiated connection process for peer %s:%d. Peer socket: %d. Current num_fds: %d, num_peers: %d\n",
                            peer_ip_log_str, ntohs(peer_addr_sa.sin_port), new_sock, *get_num_fds(), *get_num_peers());
                    fflush(stderr);
                }
            } else {
                if (get_args().debug_mode) {
                    fprintf(stderr, "[BTCLIENT_CONNECT_PEERS]: Failed to add peer %s:%d (sock_fd: %d).\n", peer_ip_log_str, ntohs(peer_addr_sa.sin_port), new_sock);
                    fflush(stderr);
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    printf(CLEAR_SCREEN);        // Clear the terminal screen for progress bar
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

    // TODO: check buffer size allocation
    char *buffer = malloc(1024 * 1024 * 10); 
    if (!buffer) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Error: Failed to allocate buffer for torrent file.\n"); 
        fflush(stderr);
        exit(1);
    }

    int bytes_read = read_torrent_file(filename, buffer, 1024 * 1024 * 10);
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

    // srand(time(NULL));
    memcpy(client_peer_id, PEER_ID, sizeof(client_peer_id));

    TrackerResponse response = tracker_get(current_torrent->announce, current_torrent->info_hash, client_peer_id, args.port, 0, 0, total_len);
    if (get_args().debug_mode) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Initial Tracker Response -> Interval: %d, Complete: %d, Incomplete: %d, Num Peers: %d\n",
                response.interval, response.complete, response.incomplete, response.num_peers);
        fflush(stderr);
    }

    last_tracker_request_time = time(NULL);
    tracker_interval_seconds = response.interval;
    if (get_args().debug_mode) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Tracker interval set to %d seconds.\n", tracker_interval_seconds);
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

    const char *output_filename_base = current_torrent->info.name ? current_torrent->info.name : "downloaded_file";
    char output_filename[1024];
    snprintf(output_filename, sizeof(output_filename), "%s", output_filename_base);

    if (piece_manager_init(current_torrent, output_filename) != 0) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Error: Failed to initialize piece manager.\n");
        fflush(stderr);
        free_tracker_response(&response); 
        torrent_free(current_torrent);
        exit(1);
    }

    connect_peers(num_peers_from_tracker, response); // This was the first call to connect_peers

    if (get_args().debug_mode) {
        fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Entering main poll loop. Initial num_fds: %d, num_peers: %d\n", *get_num_fds(), *get_num_peers());
        fflush(stderr);
    }

    printf("\n%s DOWNLOAD PROGRESS%s\n", BLUE_TEXT, RESET_TEXT);
    print_progress_bar(total_len > 0 ? (double)piece_manager_get_bytes_downloaded_total() / total_len : 0.0);
    time_t current_time = time(NULL);
    last_optimistic_unchoke_time = current_time;
    last_choke_time = current_time;

    while (1) {
        optimistic_unchoke();
        choke_peer();

        // MODIFIED: Debug log to include time until next tracker refresh
        if (get_args().debug_mode && (*get_num_fds() > 1 || *get_num_peers() > 0)) {
            fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Polling %d FDs. Active Peers: %d. Downloaded: %lu / %ld (%.2f%%). Tracker refresh in %ld s.\n",
                *get_num_fds(), *get_num_peers(), piece_manager_get_bytes_downloaded_total(), total_len,
                total_len > 0 ? (double)piece_manager_get_bytes_downloaded_total() * 100.0 / total_len : 0.0,
                (last_tracker_request_time + tracker_interval_seconds) - time(NULL) > 0 ? (last_tracker_request_time + tracker_interval_seconds) - time(NULL) : 0);
            fflush(stderr);
        }

        // Periodic tracker re-query logic
        if (time(NULL) - last_tracker_request_time >= tracker_interval_seconds) {
            if (get_args().debug_mode) {
                fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Tracker interval elapsed (%ds). Re-contacting tracker.\n", tracker_interval_seconds);
                fflush(stderr);
            }

            long downloaded_for_tracker = piece_manager_get_bytes_downloaded_total();
            long left_for_tracker = piece_manager_get_bytes_left_total();
            long uploaded_for_tracker = 0; // Placeholder, update if upload tracking is added

            TrackerResponse new_tracker_resp = tracker_get(current_torrent->announce, 
                current_torrent->info_hash, client_peer_id, args.port, uploaded_for_tracker, downloaded_for_tracker, left_for_tracker);

            if (get_args().debug_mode) {
                fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: New Tracker Response -> Interval: %d, Complete: %d, Incomplete: %d, Num Peers: %d\n",
                        new_tracker_resp.interval, new_tracker_resp.complete, new_tracker_resp.incomplete, new_tracker_resp.num_peers);
                fflush(stderr);
            }

            tracker_interval_seconds = new_tracker_resp.interval;
             if (get_args().debug_mode) {
                fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Next tracker refresh interval set to %d seconds.\n", tracker_interval_seconds);
                fflush(stderr);
            }

            if (new_tracker_resp.num_peers > 0 && *get_num_peers() < MAX_PEERS) {
                Peer *existing_peers_array = get_peers();
                int current_num_peers = *get_num_peers();
                Peer *candidate_peers_for_connection = malloc(new_tracker_resp.num_peers * sizeof(Peer));
                int num_candidate_peers_to_connect = 0;

                if (candidate_peers_for_connection) {
                    for (int k = 0; k < new_tracker_resp.num_peers; k++) {
                        // Ensure we don't exceed MAX_PEERS considering already connected and newly found candidates
                        if (current_num_peers + num_candidate_peers_to_connect >= MAX_PEERS) {
                             if (get_args().debug_mode) {
                                fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: MAX_PEERS would be exceeded by adding more candidates, stopping peer scan from tracker.\n");
                                fflush(stderr);
                            }
                            break;
                        }

                        bool already_connected = false;
                        for (int j = 0; j < current_num_peers; j++) {
                            if (existing_peers_array[j].address == htonl(new_tracker_resp.peers[k].address) &&
                                existing_peers_array[j].port == htons(new_tracker_resp.peers[k].port)) {
                                already_connected = true;
                                break;
                            }
                        }
                        if (!already_connected) {
                            if (get_args().debug_mode) {
                                char new_peer_ip_str[INET_ADDRSTRLEN];
                                struct in_addr new_peer_addr_struct;
                                new_peer_addr_struct.s_addr = htonl(new_tracker_resp.peers[k].address);
                                inet_ntop(AF_INET, &new_peer_addr_struct, new_peer_ip_str, INET_ADDRSTRLEN);
                                fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Tracker provided new peer %s:%u for connection attempt.\n",
                                        new_peer_ip_str, new_tracker_resp.peers[k].port);
                                fflush(stderr);
                            }
                            candidate_peers_for_connection[num_candidate_peers_to_connect++] = new_tracker_resp.peers[k];
                        }
                    }

                    if (num_candidate_peers_to_connect > 0) {
                        TrackerResponse temp_connect_arg_response;
                        temp_connect_arg_response.peers = candidate_peers_for_connection;
                        temp_connect_arg_response.num_peers = num_candidate_peers_to_connect;
                        temp_connect_arg_response.interval = new_tracker_resp.interval; 
                        temp_connect_arg_response.complete = new_tracker_resp.complete; 
                        temp_connect_arg_response.incomplete = new_tracker_resp.incomplete;
                        
                        connect_peers(num_candidate_peers_to_connect, temp_connect_arg_response);
                    }
                    free(candidate_peers_for_connection);
                } else if (new_tracker_resp.num_peers > 0) { // Malloc failed but there were peers to process
                     if (get_args().debug_mode) {
                        fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Failed to allocate memory for candidate peers list. Skipping new connections this cycle.\n");
                        fflush(stderr);
                    }
                }
            }
            free_tracker_response(&new_tracker_resp); 
            last_tracker_request_time = time(NULL);
        }

        // Enable endgame mode if applicable
        uint64_t bytes_left = piece_manager_get_bytes_left_total();
        if (!endgame && bytes_left > 0 && (bytes_left <= DEFAULT_BLOCK_LENGTH * MAX_OUTSTANDING_REQUESTS * 100)) {
            endgame = true;
            if (get_args().debug_mode) {fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Entering ENDGAME MODE, broadcasting remaining blocks to all peers\n"); fflush(stderr);}
            // Broadcast every missing block request to every peer
            uint32_t total_pieces = piece_manager_get_total_pieces_count();
            for (uint32_t p_idx = 0; p_idx < total_pieces; ++p_idx) {
                if (piece_manager_get_piece_state(p_idx) == PIECE_STATE_MISSING || piece_manager_get_piece_state(p_idx) == PIECE_STATE_PENDING) {
                    uint32_t block_begin, block_length;
                    // Ignore the piece->block_requested guard in endgame
                    while (piece_manager_get_block_to_request_from_piece(p_idx, &block_begin, &block_length)) {
                        // Send to all peers that have it
                        for (int i = 0; i < *get_num_peers(); ++i) {
                            Peer *peer = &get_peers()[i];
                            bool has_piece = (peer->bitfield && ((peer->bitfield[p_idx / 8] >> (7 - (p_idx % 8))) & 1));
                            if (peer->handshake_done && !peer->choked && has_piece) {
                                peer_manager_send_request(peer, p_idx, block_begin, block_length);
                            }
                        }
                    }
                }
            }
        }
        
        int poll_timeout_ms = 1000; // 1 second timeout
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

        if (fds[0].revents & POLLIN) {
            if (get_args().debug_mode) {
                fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Incoming connection detected on listen socket %d.\n", fds[0].fd);
                fflush(stderr);
            }
            if (*get_num_peers() < MAX_PEERS) {
                 peer_manager_add_peer(*current_torrent, NULL, 0); // Will attempt to accept one connection
            } else {
                 if (get_args().debug_mode) {
                    fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: MAX_PEERS reached, not accepting new incoming connection for now.\n");
                    fflush(stderr);
                }
            }
        }

        // Handle events for connected peers
        for (int i = 1; i < *get_num_fds(); ) { // Loop carefully due to potential peer removal
            Peer *current_peer_ptr = &peers[i - 1]; // Pointer to the peer structure for fds[i]
            int peer_log_idx = i - 1; // For logging, corresponds to index in peers array

            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                if (get_args().debug_mode) {
                    fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Peer at fds[%d] (socket %d, peer_idx %d) has error/hup/nval event (0x%x). Removing peer.\n",
                            i, fds[i].fd, peer_log_idx, fds[i].revents); fflush(stderr);
                }
                peer_manager_remove_peer(current_peer_ptr); // This compacts fds and peers
                continue; // num_fds changed, so re-evaluate loop condition and current fds[i]
            }

            if (fds[i].revents & POLLIN) {
                // current_peer_ptr should still be valid unless POLLOUT removed it.
                // If it was removed by POLLOUT, `continue` was hit.
                if (get_args().debug_mode) {
                    fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Data available from peer_idx %d (socket %d).\n",
                            peer_log_idx, fds[i].fd);
                    fflush(stderr);
                }
                int receive_status = peer_manager_receive_messages(current_peer_ptr);
                if (receive_status == 0) { // 0 means peer disconnected or error requiring removal
                    if (get_args().debug_mode) {
                        fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Peer_idx %d (socket %d) disconnected or error in receive. Removing.\n",
                                peer_log_idx, fds[i].fd);
                        fflush(stderr);
                    }
                    peer_manager_remove_peer(current_peer_ptr);
                    continue;
                } else if (receive_status > 0) { // Data received
                     print_progress_bar(total_len > 0 ? (double)piece_manager_get_bytes_downloaded_total() / total_len : 0.0);
                }
                // if receive_status == -1 (EAGAIN/EWOULDBLOCK or other non-fatal error), continue polling
            }

            if (i >= *get_num_fds()) { // Check bounds as num_fds might have changed
                break; // Or continue to next poll() cycle
            }
            // current_peer_ptr = &peers[i - 1];
            if (current_peer_ptr->handshake_done && !current_peer_ptr->choked && current_peer_ptr->is_interesting && current_peer_ptr->bitfield != NULL) {
                while (current_peer_ptr->num_outstanding_requests < MAX_OUTSTANDING_REQUESTS) {
                    uint32_t block_begin_offset;
                    uint32_t block_length;
                    bool found_block_to_request_this_iteration = false;

                    uint32_t total_pieces_in_torrent = piece_manager_get_total_pieces_count();
                    for (uint32_t p_idx = 0; p_idx < total_pieces_in_torrent; ++p_idx) {
                        if (piece_manager_get_piece_state(p_idx) == PIECE_STATE_MISSING || piece_manager_get_piece_state(p_idx) == PIECE_STATE_PENDING) {
                            bool peer_has_this_piece = false;
                            if (current_peer_ptr->bitfield && (p_idx / 8) < current_peer_ptr->bitfield_bytes) {
                                peer_has_this_piece = (current_peer_ptr->bitfield[p_idx / 8] >> (7 - (p_idx % 8))) & 1;
                            }

                            if (peer_has_this_piece) {
                                if (piece_manager_get_block_to_request_from_piece(p_idx, &block_begin_offset, &block_length)) {
                                    if (get_args().debug_mode) {
                                        fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Requesting from peer_idx %d (sock %d): Piece %u, Offset %u, Length %u\n",
                                                peer_log_idx, current_peer_ptr->sock_fd, p_idx, block_begin_offset, block_length);
                                        fflush(stderr);
                                    }
                                    if (peer_manager_send_request(current_peer_ptr, p_idx, block_begin_offset, block_length) != 0) {
                                         if (get_args().debug_mode) { fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Failed to send REQUEST to peer_idx %d for Piece %u.\n",
                                            peer_log_idx, p_idx);
                                            fflush(stderr);
                                        }
                                    }
                                    found_block_to_request_this_iteration = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (!found_block_to_request_this_iteration) {
                        break;
                    }
                }
            } else if (current_peer_ptr->handshake_done && current_peer_ptr->bitfield != NULL && !current_peer_ptr->is_interesting) {
                uint32_t total_pieces_in_torrent = piece_manager_get_total_pieces_count();
                for (uint32_t p_idx = 0; p_idx < total_pieces_in_torrent; ++p_idx) {
                    if (piece_manager_get_piece_state(p_idx) != PIECE_STATE_HAVE) {
                        bool peer_has_this_piece = false;
                        if (current_peer_ptr->bitfield && (p_idx / 8) < current_peer_ptr->bitfield_bytes) {
                            peer_has_this_piece = (current_peer_ptr->bitfield[p_idx / 8] >> (7 - (p_idx % 8))) & 1;
                        }
                        if (peer_has_this_piece) {
                            if (get_args().debug_mode) { fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: Peer_idx %d (sock %d) has pieces we need. Sending INTERESTED.\n", peer_log_idx, current_peer_ptr->sock_fd); fflush(stderr); }
                            peer_manager_send_interested(current_peer_ptr);
                            break;
                        }
                    }
                }
            }
            i++; // Move to the next fd ONLY if no peer was removed 
        }

        peer_manager_send_keep_alives();
        // TODO: for uploads to work we should not be breaking when we're done downloading
        if (piece_manager_is_download_complete()) {
            print_progress_bar(1.0); // Ensure progress bar shows 100%
            printf("\n");
            fprintf(stdout, GREEN_TEXT "[BTCLIENT_MAIN_LOOP]: ****** Download complete! Output file: %s ******" RESET_TEXT "\n", output_filename);
            fflush(stdout);
             if (get_args().debug_mode) {
                fprintf(stderr, "[BTCLIENT_MAIN_LOOP]: ****** Download complete! Output file: %s ******\n", output_filename);
                fflush(stderr);
            }
            break;
        }
    }

    // Free initial tracker response (original variable name was 'response')
    free_tracker_response(&response);
    if (get_args().debug_mode) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Freed initial tracker response data.\n");
        fflush(stderr);
    }

    printf("\n");                // Exit progress bar cleanly

    if (get_args().debug_mode) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Exited main poll loop.\n"); fflush(stderr);
    }

    if (get_args().debug_mode) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Cleaning up peer connections...\n");
        fflush(stderr);
    }
    // MODIFIED: Cleanup loop 
    for (int i_cleanup = (*get_num_fds()) - 1; i_cleanup >= 1; i_cleanup--) {
         // peers array index is fds index - 1
        if (i_cleanup -1 < *get_num_peers() && i_cleanup -1 >= 0) { // Check if peer exists at this index
            Peer *peer_to_remove = &peers[i_cleanup-1];
            if (get_args().debug_mode) {
                char peer_ip_str[INET_ADDRSTRLEN];
                struct in_addr peer_addr_struct = { .s_addr = peer_to_remove->address }; // address is NBO
                inet_ntop(AF_INET, &peer_addr_struct, peer_ip_str, INET_ADDRSTRLEN);
                fprintf(stderr, "[BTCLIENT_MAIN]: Removing peer %s:%u (socket %d, peer_idx %d) during final cleanup.\n",
                        peer_ip_str, ntohs(peer_to_remove->port), fds[i_cleanup].fd, (i_cleanup-1));
                fflush(stderr);
            }
            peer_manager_remove_peer(peer_to_remove);
        } else if (fds[i_cleanup].fd != -1) { 
             if (get_args().debug_mode) {
                fprintf(stderr, "[BTCLIENT_MAIN]: Discrepancy or already closed fd at fds index %d during cleanup. num_peers: %d, fd: %d\n",
                         i_cleanup, *get_num_peers(), fds[i_cleanup].fd);
                fflush(stderr);
            }
            // Ensure fd is closed if it wasn't handled by peer_manager_remove_peer (e.g., if arrays got desynced somehow)
            // peer_manager_remove_peer should have closed fds[i_cleanup].fd if a peer was associated.
            // This is a fallback.
            if (fds[i_cleanup].fd != -1) { // Double check as peer_manager_remove_peer should set it to -1 or remove the entry
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

    if (fds[0].fd != -1) { // Close listen socket
        if (get_args().debug_mode) {
            fprintf(stderr, "[BTCLIENT_MAIN]: Closing listen socket %d.\n", fds[0].fd);
            fflush(stderr);
        }
        close(fds[0].fd);
        fds[0].fd = -1;
    }
    // MODIFIED: reset global counts
    *get_num_fds() = 0;
    *get_num_peers() = 0;

    if (get_args().debug_mode) {
        fprintf(stderr, "[BTCLIENT_MAIN]: Client shutting down.\n");
        fflush(stderr);
        // fclose(stderr) // if stderr was freopen'd to debug.log. This happens automatically on exit for streams opened by the process.
    }
    return 0;
}