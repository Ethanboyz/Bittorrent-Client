/**
 * Demo program for parsing and displaying torrent file information -- and how to use the getters.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include "../include/torrent_parser.h"
 
 #define BUFFER_SIZE (1024 * 1024) // 1MB buffer -- torrent files are usually small so this should be enough
 
 void print_usage(const char *program_name) {
     printf("Usage: %s <torrent_file>\n", program_name);
     printf("  <torrent_file>  Path to a .torrent file to parse\n");
 }
 
 void print_torrent_info(const Torrent *torrent) {
     printf("\n=== Getters Demo ===\n");
     printf("Announce URL: %s\n", torrent_get_announce(torrent));
     printf("Creation Date: %s", ctime(&torrent->creation_date));
     printf("Comment: %s\n", torrent_get_comment(torrent));
     printf("Created By: %s\n", torrent_get_created_by(torrent));
     printf("Encoding: %s\n", torrent_get_encoding(torrent));
     
     printf("\nPiece Info:\n");
     printf("Piece Length: %ld bytes\n", torrent_get_piece_length(torrent));
     printf("Total Pieces: %d\n", torrent_get_piece_count(torrent));
     
     if (torrent->info.mode_type == MODE_SINGLE_FILE) {
         printf("\nSingle File Mode:\n");
         printf("File Name: %s\n", torrent->info.name);
         printf("File Size: %ld bytes\n", torrent->info.mode.single_file.length);
     } else {
         printf("\nMulti-File Mode:\n");
         printf("Directory Name: %s\n", torrent->info.name);
         printf("Total Size: %ld bytes\n", torrent->info.mode.multi_file.total_length);
         printf("File Count: %d\n", torrent->info.mode.multi_file.files_count);
     }
 }
 
 int main(int argc, char *argv[]) {
     if (argc != 2) {
         print_usage(argv[0]);
         return 1;
     }
 
     const char *filename = argv[1];
     
     // ensure the file has a .torrent extension
     size_t len = strlen(filename);
     if (len < 8 || strcmp(filename + len - 8, ".torrent") != 0) {
         fprintf(stderr, "Warning: File doesn't have a .torrent extension\n");
     }
 
     // allocate buffer for reading the torrent file
     char *buffer = malloc(BUFFER_SIZE);
     if (!buffer) {
         fprintf(stderr, "Error: Failed to allocate memory\n");
         return 1;
     }


     // read the torrent file into the buffer
     int bytes_read = read_torrent_file(filename, buffer, BUFFER_SIZE);
     if (bytes_read < 0) {
         free(buffer);
         return 1;
     }
     printf("Successfully read %d bytes from %s\n\n", bytes_read, filename);
 

     // parse and get our datastructure
     Torrent *torrent = NULL;
     int result = parse_torrent_file(buffer, bytes_read, &torrent);
     
     if (result == 0 && torrent != NULL) {
         // demonstrate the getters
         print_torrent_info(torrent);
         torrent_free(torrent);
     }
 
     free(buffer);
     return result;
 }