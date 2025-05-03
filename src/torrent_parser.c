/**
 * Implementation of BitTorrent file parsing functions
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <time.h>
 #include "../include/torrent_parser.h"
 
// open, ensure file size is less than buffer size, and finally read file into buffer
 int read_torrent_file(const char *filename, char *buffer, int buffer_size) {
     FILE *file = fopen(filename, "rb");
     if (!file) {
         perror("Error opening torrent file");
         return -1;
     }
     
     // Get file size
     fseek(file, 0, SEEK_END);
     long file_size = ftell(file);
     fseek(file, 0, SEEK_SET);
     if (file_size > buffer_size) {
         fprintf(stderr, "Torrent file too large for buffer (size: %ld, buffer: %d) -- this is a const we define in torrent_demo.c\n", 
                 file_size, buffer_size);
         fclose(file);
         return -1;
     }
     
     // Read file content into buffer
     size_t bytes_read = fread(buffer, 1, file_size, file);
     fclose(file);
     if (bytes_read != (size_t)file_size) {
         perror("Error reading torrent file");
         return -1;
     }
     
     return (int)bytes_read;
 }
 



 // print how many pieces there are and the first 3 pieces -- you can change this to print more or less
 void print_pieces(const char *pieces, int len) {
     printf("    Pieces: (%d pieces, each 20 bytes)\n", len / 20);
     
     // Print the first few pieces, then indicate there are more
     for (int i = 0; i < len && i < 60; i += 20) {
         printf("      - ");
         for (int j = 0; j < 20 && (i + j) < len; j++) {
             printf("%02x", (unsigned char)pieces[i + j]);
         }
         printf("\n");
         
         // If there are many pieces, just show the first couple
         if (i == 40 && len > 80) {
             printf("      ... (%d more pieces)\n", (len / 20) - 3);
             break;
         }
     }
 }
 



// make UNIX epoch format timestamp human-readable -- not that useful for us
 char* format_timestamp(long int timestamp, char *buffer, int buffer_size) {
     time_t time = (time_t)timestamp;
     struct tm *timeinfo = localtime(&time);
     
     if (timeinfo == NULL) {
         snprintf(buffer, buffer_size, "Invalid timestamp");
         return buffer;
     }
     
     strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", timeinfo);
     return buffer;
 }
 

 // helper functions
 static void parse_info_dict(bencode_t *ben);
 static void parse_files_list(bencode_t *ben);
 

 int parse_torrent_file(const char *torrent_data, int len) {
     bencode_t ben;
     
     // Initialize bencode parser with torrent data -- take a look at the bencode.h file for more details
     bencode_init(&ben, torrent_data, len);
     
     // Validate that this is a dictionary (all torrent files are bencoded dicts)
     if (!bencode_is_dict(&ben)) {
         fprintf(stderr, "Error: Invalid torrent file format (not a dictionary)\n");
         return -1;
     }
     
     printf("Torrent File Contents:\n");
     printf("======================\n\n");
     
     // Process each key in the top-level dictionary
     while (bencode_dict_has_next(&ben)) {
         bencode_t ben_item;
         const char *key;
         int key_len;
         
         if (!bencode_dict_get_next(&ben, &ben_item, &key, &key_len)) {
             fprintf(stderr, "Error parsing torrent dictionary\n");
             return -1;
         }
         
         // Print the key
         printf("* %.*s: ", key_len, key);
         
         // Handle different value types
         if (bencode_is_dict(&ben_item)) {
             // Special handling for "info" dictionary
             if (key_len == 4 && strncmp(key, "info", 4) == 0) {
                 printf("(dictionary)\n");
                 parse_info_dict(&ben_item);
             } else {
                 printf("(dictionary - contents not shown)\n");
             }
         } 
         else if (bencode_is_list(&ben_item)) {
             // Handle announce-list separately
             if (key_len == 12 && strncmp(key, "announce-list", 12) == 0) {
                 printf("(list of tracker URLs)\n");
                 
                 bencode_t list_item;
                 int tier = 1;
                 
                 while (bencode_list_has_next(&ben_item)) {
                     bencode_list_get_next(&ben_item, &list_item);
                     
                     if (bencode_is_list(&list_item)) {
                         printf("  Tier %d:\n", tier++);
                         
                         bencode_t url_item;
                         while (bencode_list_has_next(&list_item)) {
                             bencode_list_get_next(&list_item, &url_item);
                             
                             if (bencode_is_string(&url_item)) {
                                 const char *url;
                                 int url_len;
                                 
                                 bencode_string_value(&url_item, &url, &url_len);
                                 printf("    - %.*s\n", url_len, url);
                             }
                         }
                     }
                 }
             } else {
                 printf("(list)\n");
             }
         } 
         else if (bencode_is_string(&ben_item)) {
             const char *str;
             int str_len;
             
             bencode_string_value(&ben_item, &str, &str_len);
             
             // For announce URL
             if (key_len == 8 && strncmp(key, "announce", 8) == 0) {
                 printf("%.*s\n", str_len, str);
             } 
             // For comment and created by fields
             else if ((key_len == 7 && strncmp(key, "comment", 7) == 0) ||
                     (key_len == 10 && strncmp(key, "created by", 10) == 0) ||
                     (key_len == 8 && strncmp(key, "encoding", 8) == 0)) {
                 printf("%.*s\n", str_len, str);
             } 
             else {
                 printf("(string, %d bytes)\n", str_len);
             }
         } 
         else if (bencode_is_int(&ben_item)) {
             long int val;
             
             bencode_int_value(&ben_item, &val);
             
             // Handle creation date specially
             if (key_len == 13 && strncmp(key, "creation date", 13) == 0) {
                 char date_buf[64];
                 format_timestamp(val, date_buf, sizeof(date_buf));
                 printf("%ld (%s)\n", val, date_buf);
             } else {
                 printf("%ld\n", val);
             }
         } 
         else {
             printf("(unknown type)\n");
         }
     }
     
     return 0;
 }
 


 static void parse_info_dict(bencode_t *ben) {
     bencode_t ben_item;
     const char *key;
     int key_len;
     int single_file_mode = 1;  // Assume single file mode by default
     
     // First pass to determine if this is single or multi file mode
     bencode_t ben_copy;
     bencode_clone(ben, &ben_copy);
     
     while (bencode_dict_has_next(&ben_copy)) {
         if (!bencode_dict_get_next(&ben_copy, &ben_item, &key, &key_len)) {
             fprintf(stderr, "Error parsing info dictionary\n");
             return;
         }
         
         if (key_len == 5 && strncmp(key, "files", 5) == 0) {
             single_file_mode = 0;  // Multi-file mode
             break;
         }
     }
     
     // Reset and do the actual parsing
     bencode_clone(ben, &ben_copy);
     
     printf("  Info Dictionary:\n");
     
     while (bencode_dict_has_next(&ben_copy)) {
         if (!bencode_dict_get_next(&ben_copy, &ben_item, &key, &key_len)) {
             fprintf(stderr, "Error parsing info dictionary\n");
             return;
         }
         
         // Handle common info dict keys
         if (key_len == 4 && strncmp(key, "name", 4) == 0) {
             if (bencode_is_string(&ben_item)) {
                 const char *name;
                 int name_len;
                 
                 bencode_string_value(&ben_item, &name, &name_len);
                 printf("    Name: %.*s\n", name_len, name);
             }
         }
         else if (key_len == 12 && strncmp(key, "piece length", 12) == 0) {
             if (bencode_is_int(&ben_item)) {
                 long int val;
                 
                 bencode_int_value(&ben_item, &val);
                 printf("    Piece Length: %ld bytes", val);
                 
                 // Convert to more readable format for larger sizes
                 if (val >= 1024) {
                     if (val >= 1024 * 1024) {
                         printf(" (%.2f MB)", (float)val / (1024 * 1024));
                     } else {
                         printf(" (%.2f KB)", (float)val / 1024);
                     }
                 }
                 printf("\n");
             }
         }
         else if (key_len == 6 && strncmp(key, "pieces", 6) == 0) {
             if (bencode_is_string(&ben_item)) {
                 const char *pieces;
                 int pieces_len;
                 
                 bencode_string_value(&ben_item, &pieces, &pieces_len);
                 print_pieces(pieces, pieces_len);
             }
         }
         else if (key_len == 7 && strncmp(key, "private", 7) == 0) {
             if (bencode_is_int(&ben_item)) {
                 long int val;
                 
                 bencode_int_value(&ben_item, &val);
                 printf("    Private: %ld (%s)\n", val, val ? "Yes" : "No");
             }
         }
         else if (key_len == 6 && strncmp(key, "length", 6) == 0 && single_file_mode) {
             if (bencode_is_int(&ben_item)) {
                 long int val;
                 
                 bencode_int_value(&ben_item, &val);
                 printf("    Length: %ld bytes", val);
                 
                 // Convert to more readable format
                 if (val >= 1024) {
                     if (val >= 1024 * 1024 * 1024) {
                         printf(" (%.2f GB)", (float)val / (1024 * 1024 * 1024));
                     } else if (val >= 1024 * 1024) {
                         printf(" (%.2f MB)", (float)val / (1024 * 1024));
                     } else {
                         printf(" (%.2f KB)", (float)val / 1024);
                     }
                 }
                 printf("\n");
             }
         }
         else if (key_len == 6 && strncmp(key, "md5sum", 6) == 0 && single_file_mode) {
             if (bencode_is_string(&ben_item)) {
                 const char *md5;
                 int md5_len;
                 
                 bencode_string_value(&ben_item, &md5, &md5_len);
                 printf("    MD5Sum: %.*s\n", md5_len, md5);
             }
         }
         else if (key_len == 5 && strncmp(key, "files", 5) == 0 && !single_file_mode) {
             printf("    Files: (multi-file mode)\n");
             parse_files_list(&ben_item);
         }
     }
 }
 

 
 static void parse_files_list(bencode_t *ben) {
     if (!bencode_is_list(ben)) {
         fprintf(stderr, "Error: 'files' is not a list\n");
         return;
     }
     
     bencode_t file_dict;
     long total_size = 0;
     int file_count = 0;
     
     while (bencode_list_has_next(ben)) {
         bencode_list_get_next(ben, &file_dict);
         file_count++;
         
         if (bencode_is_dict(&file_dict)) {
             bencode_t ben_item;
             const char *key;
             int key_len;
             long file_length = 0;
             printf("      File %d:\n", file_count);
             
             while (bencode_dict_has_next(&file_dict)) {
                 bencode_dict_get_next(&file_dict, &ben_item, &key, &key_len);
                 
                 if (key_len == 6 && strncmp(key, "length", 6) == 0) {
                     if (bencode_is_int(&ben_item)) {
                         bencode_int_value(&ben_item, &file_length);
                         total_size += file_length;
                         
                         printf("        Length: %ld bytes", file_length);
                         if (file_length >= 1024) {
                             if (file_length >= 1024 * 1024 * 1024) {
                                 printf(" (%.2f GB)", (float)file_length / (1024 * 1024 * 1024));
                             } else if (file_length >= 1024 * 1024) {
                                 printf(" (%.2f MB)", (float)file_length / (1024 * 1024));
                             } else {
                                 printf(" (%.2f KB)", (float)file_length / 1024);
                             }
                         }
                         printf("\n");
                     }
                 }
                 else if (key_len == 6 && strncmp(key, "md5sum", 6) == 0) {
                     if (bencode_is_string(&ben_item)) {
                         const char *md5;
                         int md5_len;
                         
                         bencode_string_value(&ben_item, &md5, &md5_len);
                         printf("        MD5Sum: %.*s\n", md5_len, md5);
                     }
                 }
                 else if (key_len == 4 && strncmp(key, "path", 4) == 0) {
                     if (bencode_is_list(&ben_item)) {
                         bencode_t path_item;
                         printf("        Path: ");
                         
                         int first = 1;
                         while (bencode_list_has_next(&ben_item)) {
                             bencode_list_get_next(&ben_item, &path_item);
                             
                             if (bencode_is_string(&path_item)) {
                                 const char *path_component;
                                 int path_len;
                                 
                                 bencode_string_value(&path_item, &path_component, &path_len);
                                 
                                 if (!first) printf("/");
                                 printf("%.*s", path_len, path_component);
                                 first = 0;
                             }
                         }
                         printf("\n");
                     }
                 }
             }
         }
     }
     
     printf("      Total Size: %ld bytes", total_size);
     if (total_size >= 1024) {
         if (total_size >= 1024 * 1024 * 1024) {
             printf(" (%.2f GB)", (float)total_size / (1024 * 1024 * 1024));
         } else if (total_size >= 1024 * 1024) {
             printf(" (%.2f MB)", (float)total_size / (1024 * 1024));
         } else {
             printf(" (%.2f KB)", (float)total_size / 1024);
         }
     }
     printf("\n");
 }