Component: Tracker Communication

Relevant Files: tracker.h, tracker.c, peer_manager.h, bencode.h

Goal: This component handles communication with the BitTorrent tracker. It constructs and sends HTTP(S) GET requests to the tracker's announce URL, including necessary parameters like the info hash, peer ID, listening port, and download progress. It receives the tracker's bencoded response, which contains information such as the interval for re-announcing, the number of seeds and leechers, and most importantly, a list of peers participating in the torrent.

Usage Notes: The btclient.c uses the http_get() function to send an announcement request to the tracker and receive a TrackerResponse structure. This structure contains the list of peers and other relevant information. The free_tracker_response() function should be used to deallocate the memory used by the peer list within the response.


**TODO Notes:**

- UDP Tracker Support: The parse_announce() function recognizes udp:// URLs, but the http_get() function only implements HTTP and HTTPS communication. 

- Scrape Request Implementation: The scrape protocol allows the client to get torrent statistics (seeders/leechers)