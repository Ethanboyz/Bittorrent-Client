Component: Main Client Application / Core Loop

Relevant Files: btclient.h, btclient.c, arg_parser.h, torrent_parser.h, peer_manager.h, tracker.h


Goal: Serve as the application's entry point (main), handles command-line arguments, loads and parses the torrent file, communicates with the tracker to discover peers, sets up the main listening socket, and contains the primary event loop (poll) to manage network activity across all connected peers. 

Usage Notes: The main function initializes the process by calling arg_parseopt(). It then reads and parses the torrent file using torrent_parser functions, obtains peer information from the tracker via tracker functions, and sets up the listening socket using client_listen(). The infinite while loop uses poll() to wait for events on sockets. When events occur it should attempt to add a new peer using peer_manager_add_peer().


**TODO Notes:**

- Integration with Other Components: WE NEED TO *connect the parsed torrent data, the list of peers from the tracker, the piece management logic, and the peer communication logic.*

- Currently, the torrent parsing and tracker response handling are present but not fully integrated into the main poll loop for active downloading.

- Handle Incoming Peer Messages: The poll loop checks for POLLIN on connected peer sockets (fds[i] for i > 0) but the code to receive and process actual BitTorrent messages from peers is missing. 


i think everything rn is dependent on peer management 