Component: Peer Connection and Messaging Management

Relevant Files: peer_manager.h, peer_manager.c, torrent_parser.h, btclient.h, piece_manager.h


Goal: This component manages the lifecycle of individual peer connections. It handles establishing outgoing connections to peers obtained from the tracker and accepting incoming connections from other clients. It is responsible for sending standard BitTorrent messages (handshake, choke, unchoke, interested, not interested, bitfield, request, piece, cancel, port, keepalive) and receiving data from peers. It tracks the state of each peer relationship (choking/choked, interested/interesting) and maintains a queue of outstanding piece requests for each peer.


Usage Notes: The btclient.c uses peer_manager_add_peer() to establish connections. Once connected, other parts of the client (likely coordinated by the main loop and piece selection logic) would call functions like peer_manager_send_interested(), peer_manager_choke_peer(), peer_manager_queue_request(), and potentially peer_manager_send_keepalive_message(). The peer_manager_remove_peer() function is used to disconnect and clean up a peer. The component interacts with the global Peer and pollfd arrays managed within btclient.c via getter functions.

TODO Notes:

- Message Handling Logic: The core logic for processing received BitTorrent messages is missing (handle_peer_message()). This function needs to be implemented to interpret the message ID and payload and take appropriate actions (e.g., update peer state, record received blocks).

- Incoming Buffer Parsing: The function parse_peer_incoming_buffer(). This function is necessary to read raw data from the socket buffer, identify complete BitTorrent messages based on their length prefix, and pass them to the message handling logic (handle_peer_message()).

- Outstanding Request Dequeueing: The dequeue_outstanding() function needs implementation. It should be called when a 'piece' message is successfully received and processed to remove the corresponding request from the peer's queue of outstanding requests.

- Outstanding Request Enqueueing: While peer_manager_queue_request() sends the request message,  the actual enqueueing of the request details into the outstanding_requests circular array within the Peer struct needs to be implemented.

- Keepalive Message Sending: The peer_manager_send_keepalive_message(). This needs to be implemented to send keepalive messages periodically to prevent connections from timing out.

- Receive Messages Implementation: The peer_manager_receive_messages() function is likely intended to be called from the main poll loop when a peer socket is readable (POLLIN) to read data from the socket and feed it into the incoming buffer for parsing.


**Ethan plans on doing this -- hes the goat**