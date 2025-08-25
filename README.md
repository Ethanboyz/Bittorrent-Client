# BitTorrent Client Design

## How to run
If it's your first time running locally then do 
git submodule update --init --recursive

Example executable
./btclient -d -p 1234 -f fileName
 
-d: if included then enable debug mode
-p: port that our client will run on
-f: torrent file

## Development Plan

### Known Issues

Running some torrents causes bencode asserts to fail (torrent file dependent)
Security issues (memory handling, file I/O)

### Work in Progress
 - Send HAVE messages (not just receiving HAVE messages) for continuous uploading
 - Allow resuming download after killing the btclient process
   - Write to disk (done)
   - Record tokens
 - Rarest first implementation
 - Endgame mode
 - BitTyrant
 - Propshare
 - Multifile torrenting

## Test With:
  - Download time
  - Comparison with other clients
  - Logging (wireshark)

## Resources
- BitTorrent Protocol: https://wiki.theory.org/BitTorrentSpecification 
- UDP tracker support: https://www.bittorrent.org/beps/bep_0015.html  


