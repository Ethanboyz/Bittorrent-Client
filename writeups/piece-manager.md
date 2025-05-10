Component: Piece Download and Verification Management

Relevant Files: piece_manager.h, piece_manager.c, torrent_parser.h, hash.h, btclient.h

Goal: This component manages the state of each piece of the torrent being downloaded. It tracks which pieces the client has, which are currently being downloaded, and which are still needed. It handles receiving blocks of data for a piece, accumulating the data, verifying the complete piece against the expected SHA-1 hash, and writing verified pieces to the output file. It also provides functions for selecting pieces to request from peers and tracking download progress.

Usage Notes: The btclient.c should initialize this manager using piece_manager_init() after parsing the torrent file. When a block of data is received from a peer, piece_manager_record_block_received() is called. After all blocks for a piece are received, piece_manager_is_piece_payload_complete() will return true, triggering a call to piece_manager_verify_and_write_piece(). To decide what to request from a peer, piece_manager_select_piece_for_peer() and piece_manager_get_block_to_request_from_piece() can be used. piece_manager_get_our_bitfield() provides the client's piece availability for sending to peers. Progress can be checked using piece_manager_is_download_complete(), piece_manager_get_bytes_downloaded_total(), and piece_manager_get_bytes_left_total(). piece_manager_destroy() is used for cleanup.


**TODO Notes:**

- The algo is not optimal -- the code includes fields (peer_availability_count) and a function (piece_manager_update_peer_availability) suggesting we could implement a rarest-first piece selection strategy (requesting pieces that are least common among connected peers). However, the current piece_manager_select_piece_for_peer() function uses a simple sequential selection. The piece selection logic needs to be updated to utilize the rarest-first strategy for more efficient downloading.

