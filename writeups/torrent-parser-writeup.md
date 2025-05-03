# Torrent File Parser Design
author: jalal

## Goal
Provide a way to parse BitTorrent metainfo (.torrent) files and access torrent file content
We use the `heapless-bencode` library to handle the bencoded data format.

## Usage
- run make

- in project root run 
./torrent-demo ./pathToTorrentFile/example.torrent


## TODO and NOTES
- test various .torrent files as different ones have different structures ie single file mode vs multiple vs optional fields
- the parser itself i guess is useless except for our own understanding --> the main thing to prioritize and test is a datastructure/getters that let us easily obtain info/fields we need




