# BitTorrent Client Design doc
(we can outline our design in this readme as we go or most likely towards the end -- change this as you'd like but keep the logs section :) )

## How to run
If it's your first time running locally then do 
git submodule update --init --recursive

Example executable
./btclient -d -p 1234 -f fileName
 
-d: if included then enable debug mode
-p: port that our client will run on
-f: torrent file


## Logs (may or may not be maintained...)

- 5/13: our first working commit. I will conduct more tests but as of now, flatland-http.torrent works. There are still things to work out, like each piece being received twice and things to test like downloading from multiple peers. (Ethan)
- 5/12: when adding new peers, connect attempts can now timeout. This gives flexibility for the client to connect only to peers it can actually connect to, instead of forcing it to attempt to connect to the entire swarm. Code now runs up until the main loop, where after removing a peer, it segfaults somewhere (Ethan)
- 5/12: completed implementation to handle incoming piece messages (Ethan)
- 5/12: Tracker scrape convention implemented for HTTP and HTTPS (Priya)
- 5/11: tried to create a "cohesive-ized" btclient executable that starts to idealize how we want components to talk to each other -- I think this should be area of debugging so we know which components need their logic rechecked.
- 5/11: implemented ethans basic todos in peer manager. think we need to start thinking about what algorithims we want to incorporate since functions depend on those. (Jalal)
- 5/10: Lots of the receive message logic is implemented, still needs work responding to each msg_id. Also, needs work with updating download/upload rates for a given peer. I left pseudocode for this. As a matter of fact, wherever needs work is marked with TODO (Ethan)
- 5/10: completed a lot of the receiving messages from peers logic, still needs code to actually process the received messages (if anyone wants to help with that). I'll keep working into the night to finish the rest of everything (Ethan)
- 5/10: as per ethans suggestion, I tried to add functionality to measure download/upload rates BUT since full SEND/RECV CODE hasn't been implemented in peer manager, I don't want to mess with anything and break the current thought process since measuring rates involves measuring bytes sent (Jalal)
- 5/10: wrote some writeups of how to begin to integrate everything together -- tracker needs some udp and scrape implementation -- remaining functionality currently dependent upon peer manager -- the goat ethans got it (Jalal)
- 5/09: implemented a bunch of peer_manager functions. Still needs work with sending and receiving request and piece messages, which I'll finish tomorrow. Peer manager
also needs functionality to measure download/upload rates. If anyone wants to implement that logic (looking at you Jalal), that'll be most welcome. If not, I'm happy to do it. (Ethan)
- 5/09: finished up tracker portion for HTTP trackers. still having trouble with
HTTPS trackers, but working through it. tracker code returns a TrackerResponse
which will hold a list of Peer structs from peer_manager (Priya)
- 5/06: wrote some function skeletons for peer_manager and added some structure to btclient. Btclient will now listen for incoming connections/messages. (Ethan)
- 5/05: set up an entry point to the client and updated arg parser to take in port and filename arguments (Priya)
- 5/05: calculated info hash in torrent_parser to be used in tracker request parameters, and set up a rough header file for the tracker
communication to get list of peers (still needs to be implemented) (Priya)
- 5/04: basic arg parser (contains only an option for debug mode but feel free to add more arguments if needed) (Ethan)
- 5/03: set up some datastruct fields for torrent parsing; needs testing but seems good as of now; don't know if the current structure is optimal, but i guess its still nice to have something; we can face hell as we go :) (Jalal);
- 5/03: .torrent file parsing; need to determine some data structure or getters (Jalal);
- 5/02: team meeting; organization; discussion and plan;


## Development Plan?

1) torrent parsing
2) tracker communication (handshake)
3) piece manager
4) advanced extra cred features intertwined?


## Ideas of how to approach testing?

  - Download time
  - Comparison with other clients
  - Logging (wireshark)


## Resources

- BitTorrent Protocol: https://wiki.theory.org/BitTorrentSpecification 
- UDP tracker support: https://www.bittorrent.org/beps/bep_0015.html  


