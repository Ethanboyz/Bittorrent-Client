/**
 * @file torrent_demo.c
 * @brief Demo program for parsing and displaying torrent file information
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include "../include/torrent_parser.h"
 
 #define BUFFER_SIZE (1024 * 1024) // 1MB buffer should be enough for most torrent files
 
 void print_usage(const char *program_name) {
     printf("Usage: %s <torrent_file>\n", program_name);
     printf("  <torrent_file>  Path to a .torrent file to parse\n");
 }
 
 int main(int argc, char *argv[]) {
     if (argc != 2) {
         print_usage(argv[0]);
         return 1;
     }
     
     const char *filename = argv[1];
     
     // check if the file has a .torrent extension
     size_t len = strlen(filename);
     if (len < 8 || strcmp(filename + len - 8, ".torrent") != 0) {
         fprintf(stderr, "Warning: File doesn't have a .torrent extension\n");
     }
     
     // allocate buffer for torrent data in heap
     char *buffer = (char *)malloc(BUFFER_SIZE);
     if (!buffer) {
         fprintf(stderr, "Error: Failed to allocate memory\n");
         return 1;
     }
     
     // read the torrent file into memory
     int bytes_read = read_torrent_file(filename, buffer, BUFFER_SIZE);
     if (bytes_read < 0) {
         free(buffer);
         return 1;
     }
     
     printf("Successfully read %d bytes from %s\n\n", bytes_read, filename);
     
     // parse and print torrent information
     int result = parse_torrent_file(buffer, bytes_read);
     free(buffer);
     
     return (result == 0) ? 0 : 1;
 }