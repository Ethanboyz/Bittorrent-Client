/**
 * This file provides functions to allow us to parse and
 * display information from a BitTorrent metainfo file (.torrent)
 */

 #ifndef TORRENT_PARSER_H
 #define TORRENT_PARSER_H
 
 #include "../heapless-bencode/bencode.h"
 
 /**
  * Reads a torrent file into memory
  * 
  * @param filename Path to the torrent file
  * @param buffer Pointer to the buffer where file content will be stored
  * @param buffer_size Size of the allocated buffer
  * @return int Size of the file read, or -1 on error
  */
 int read_torrent_file(const char *filename, char *buffer, int buffer_size);
 
 /**
  * Parses and prints torrent file information in a human-readable format
  * 
  * @param torrent_data Pointer to torrent file data in memory
  * @param len Length of the torrent data
  * @return int 0 on success, non-zero on failure
  */
 int parse_torrent_file(const char *torrent_data, int len);
 
 /**
  * Prints the SHA1 hash pieces in a readable format
  * 
  * @param pieces Pointer to pieces string
  * @param len Length of the pieces string
  */
 void print_pieces(const char *pieces, int len);
 
 /**
  * Helper function to convert timestamp to readable date
  * 
  * @param timestamp UNIX timestamp
  * @param buffer Buffer to store the date string
  * @param buffer_size Size of the buffer
  * @return char* Pointer to the buffer containing the formatted date
  */
 char* format_timestamp(long int timestamp, char *buffer, int buffer_size);


 // TODO: create some datastructure to hold the torrent data or some kind of getters or something that can make it easy for us to get any required field we need when creating the communication component of a bittorent client

 
 #endif