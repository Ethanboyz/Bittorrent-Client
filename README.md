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

- 5/06: wrote some function skeletons for peer_manager and added some structure to btclient. Btclient will now listen for incoming connections/messages.
- 5/05: set up an entry point to the client and updated arg parser to take in port and filename arguments 
- 5/05: calculated info hash in torrent_parser to be used in tracker request parameters, and set up a rough header file for the tracker
communication to get list of peers (still needs to be implemented)
- 5/04: basic arg parser (contains only an option for debug mode but feel free to add more arguments if needed)
- 5/03: set up some datastruct fields for torrent parsing; needs testing but seems good as of now; don't know if the current structure is optimal, but i guess its still nice to have something; we can face hell as we go :);
- 5/03: .torrent file parsing; need to determine some data structure or getters;
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


