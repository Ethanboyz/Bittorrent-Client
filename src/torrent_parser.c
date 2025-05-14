#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "torrent_parser.h"
#include "hash.h"

// helper function prototypes -- these are tried to kept modular and are what use the bencode library we've submoduled into this repo
static void handle_announce(bencode_t *ben_item, Torrent *torrent);
static void handle_announce_list(bencode_t *ben_item, Torrent *torrent);
static void handle_creation_date(bencode_t *ben_item, Torrent *torrent);
static void handle_comment(bencode_t *ben_item, Torrent *torrent);
static void handle_created_by(bencode_t *ben_item, Torrent *torrent);
static void handle_encoding(bencode_t *ben_item, Torrent *torrent);
static void handle_info_dict(bencode_t *ben_item, Torrent *torrent);

static void calculate_info_hash(bencode_t *ben_item, Torrent *torrent);

static void parse_files_list(bencode_t *ben, TorrentInfo *info);
static char* join_path_components(bencode_t *path_list);

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
    
    // read file content into buffer
    size_t bytes_read = fread(buffer, 1, file_size, file);
    fclose(file);
    if (bytes_read != (size_t)file_size) {
        perror("Error reading torrent file");
        return -1;
    }
    
    return (int)bytes_read;
}

// Functions that will manage our data structures
Torrent* torrent_create() {
    Torrent *torrent = calloc(1, sizeof(Torrent));
    if (!torrent) return NULL;
    
    torrent->info.mode_type = MODE_UNINITIALIZED;
    return torrent;
}

void torrent_free(Torrent *torrent) {
    if (!torrent) return;

    free(torrent->announce);
    free(torrent->comment);
    free(torrent->created_by);
    free(torrent->encoding);
    
    if (torrent->announce_list) {
        for (int i = 0; i < torrent->announce_list_count; i++) {
            free(torrent->announce_list[i]);
        }
        free(torrent->announce_list);
    }

    free(torrent->info.pieces);
    free(torrent->info.name);
    
    if (torrent->info.mode_type == MODE_MULTI_FILE) {
        for (int i = 0; i < torrent->info.mode.multi_file.files_count; i++) {
            free(torrent->info.mode.multi_file.files[i].path);
            free(torrent->info.mode.multi_file.files[i].md5sum);
        }
        free(torrent->info.mode.multi_file.files);
    }

    free(torrent);
}

// THE MAIN PARSER FUNCTION 
int parse_torrent_file(const char *torrent_data, int len, Torrent** out_torrent) {
    bencode_t ben;
    Torrent *torrent = torrent_create();
    if (!torrent) return -1;

    bencode_init(&ben, torrent_data, len);

    if (!bencode_is_dict(&ben)) {
        torrent_free(torrent);
        return -1;
    }

    while (bencode_dict_has_next(&ben)) {
        bencode_t ben_item;
        const char *key;
        int key_len;

        if (!bencode_dict_get_next(&ben, &ben_item, &key, &key_len)) {
            torrent_free(torrent);
            return -1;
        }

        if (strncmp(key, "announce", key_len) == 0) {
            handle_announce(&ben_item, torrent);
        }
        else if (strncmp(key, "announce-list", key_len) == 0) {
            handle_announce_list(&ben_item, torrent);
        }
        else if (strncmp(key, "creation date", key_len) == 0) {
            handle_creation_date(&ben_item, torrent);
        }
        else if (strncmp(key, "comment", key_len) == 0) {
            handle_comment(&ben_item, torrent);
        }
        else if (strncmp(key, "created by", key_len) == 0) {
            handle_created_by(&ben_item, torrent);
        }
        else if (strncmp(key, "encoding", key_len) == 0) {
            handle_encoding(&ben_item, torrent);
        }
        else if (strncmp(key, "info", key_len) == 0) {
            handle_info_dict(&ben_item, torrent);
        }
    }

    if (out_torrent) {
        *out_torrent = torrent;
    } else {
        torrent_free(torrent);
    }

    return 0;
}

// handlers that handle the bencoding parsing and validation -- these could be areas of concern if not implemented correctly :((( -- this proj is hard
static void handle_announce(bencode_t *ben_item, Torrent *torrent) {
    if (!bencode_is_string(ben_item)) {
        return;
    }

    const char *val;
    int val_len;
    bencode_string_value(ben_item, &val, &val_len);
    
    torrent->announce = strndup(val, val_len);
}

static void handle_announce_list(bencode_t *ben_item, Torrent *torrent) {
    if (!bencode_is_list(ben_item)) {
        return;
    }
    
    // the first pass to count differet announce trackers
    bencode_t ben_count;
    bencode_clone(ben_item, &ben_count);
    torrent->announce_list_count = 0;
    while (bencode_list_has_next(&ben_count)) {
        bencode_t item;
        bencode_list_get_next(&ben_count, &item);
        torrent->announce_list_count++;
    }

    torrent->announce_list = calloc(torrent->announce_list_count, sizeof(char*));
    
    bencode_clone(ben_item, &ben_count);
    int idx = 0;
    while (bencode_list_has_next(&ben_count)) {
        bencode_t url_list;
        bencode_list_get_next(&ben_count, &url_list);
        
        if (bencode_list_has_next(&url_list)) {
            bencode_t url_item;
            bencode_list_get_next(&url_list, &url_item);
            
            const char *url;
            int url_len;
            bencode_string_value(&url_item, &url, &url_len);
            
            torrent->announce_list[idx++] = strndup(url, url_len);
        }
    }
}

static void handle_creation_date(bencode_t *ben_item, Torrent *torrent) {
    if (!bencode_is_int(ben_item)) {
        return;
    }

    long timestamp;
    bencode_int_value(ben_item, &timestamp);
    torrent->creation_date = (time_t)timestamp;
}

static void handle_comment(bencode_t *ben_item, Torrent *torrent) {
    if (!bencode_is_string(ben_item)) {
        return;
    }

    const char *val;
    int val_len;
    bencode_string_value(ben_item, &val, &val_len);
    
    torrent->comment = strndup(val, val_len);
}

static void handle_created_by(bencode_t *ben_item, Torrent *torrent) {
    if (!bencode_is_string(ben_item)) {
        return;
    }

    const char *val;
    int val_len;
    bencode_string_value(ben_item, &val, &val_len);
    
    torrent->created_by = strndup(val, val_len);
}

static void handle_encoding(bencode_t *ben_item, Torrent *torrent) {
    if (!bencode_is_string(ben_item)) {
        return;
    }

    const char *val;
    int val_len;
    bencode_string_value(ben_item, &val, &val_len);
    
    torrent->encoding = strndup(val, val_len);
}

static void handle_info_dict(bencode_t *ben_item, Torrent *torrent) {
    if (!bencode_is_dict(ben_item)) {
        return;
    }

    TorrentInfo *info = &torrent->info;
    
    // first pass to determine mode
    bencode_t ben_mode;
    bencode_clone(ben_item, &ben_mode);
    info->mode_type = MODE_SINGLE_FILE;
    while (bencode_dict_has_next(&ben_mode)) {
        bencode_t item;
        const char *key;
        int key_len;
        
        bencode_dict_get_next(&ben_mode, &item, &key, &key_len);
        if (strncmp(key, "files", key_len) == 0) {
            info->mode_type = MODE_MULTI_FILE;
            break;
        }
    }

    // second pass to do the actual parsing
    bencode_clone(ben_item, &ben_mode);
    while (bencode_dict_has_next(&ben_mode)) {
        bencode_t item;
        const char *key;
        int key_len;
        
        bencode_dict_get_next(&ben_mode, &item, &key, &key_len);
                
        if (strncmp(key, "name", key_len) == 0) {
            if (bencode_is_string(&item)) {
                const char *val;
                int val_len;
                bencode_string_value(&item, &val, &val_len);
                
                info->name = strndup(val, val_len);
            }
        }
        else if (strncmp(key, "piece length", key_len) == 0) {
            if (bencode_is_int(&item)) {
                bencode_int_value(&item, &info->piece_length);
            }
        }
        else if (strncmp(key, "pieces", key_len) == 0) {
            if (bencode_is_string(&item)) {
                const char *pieces;
                int pieces_len;
                bencode_string_value(&item, &pieces, &pieces_len);
                
                info->pieces = malloc(pieces_len);
                memcpy(info->pieces, pieces, pieces_len);
                info->pieces_count = pieces_len / 20;
            }
        }
        else if (strncmp(key, "private", key_len) == 0) {
            if (bencode_is_int(&item)) {
                long private_flag;
                bencode_int_value(&item, &private_flag);
                info->is_private = private_flag ? 1 : 0;
            }
        }
        else if (strncmp(key, "length", key_len) == 0 && info->mode_type == MODE_SINGLE_FILE) {
            if (bencode_is_int(&item)) {
                bencode_int_value(&item, &info->mode.single_file.length);
            }
        }
        else if (strncmp(key, "md5sum", key_len) == 0 && info->mode_type == MODE_SINGLE_FILE) {
            if (bencode_is_string(&item)) {
                const char *val;
                int val_len;
                bencode_string_value(&item, &val, &val_len);
                
                info->mode.single_file.md5sum = strndup(val, val_len);
            }
        }
        else if (strncmp(key, "files", key_len) == 0 && info->mode_type == MODE_MULTI_FILE) {
            if (bencode_is_list(&item)) {
                parse_files_list(&item, info);
            }
        }
    }

    calculate_info_hash(ben_item, torrent);
}

static void parse_files_list(bencode_t *ben, TorrentInfo *info) {
    if (!bencode_is_list(ben)) {
        fprintf(stderr, "Error: 'files' is not a list\n");
        return;
    }
    
    MultiFileInfo *multi = &info->mode.multi_file;
    
    // first pass to count files
    bencode_t ben_count;
    bencode_clone(ben, &ben_count);
    multi->files_count = 0;
    while (bencode_list_has_next(&ben_count)) {
        bencode_t item;
        bencode_list_get_next(&ben_count, &item);
        multi->files_count++;
    }

    // allocate files array
    multi->files = calloc(multi->files_count, sizeof(TorrentFile));
    
    // now do parsing
    bencode_clone(ben, &ben_count);
    int idx = 0;
    while (bencode_list_has_next(&ben_count)) {
        bencode_t file_dict;
        bencode_list_get_next(&ben_count, &file_dict);
        
        TorrentFile *file = &multi->files[idx++];
        bencode_t ben_file;
        bencode_clone(&file_dict, &ben_file);
        
        while (bencode_dict_has_next(&ben_file)) {
            bencode_t ben_item;
            const char *key;
            int key_len;

            bencode_dict_get_next(&ben_file, &ben_item, &key, &key_len);

            if (strncmp(key, "length", key_len) == 0 && bencode_is_int(&ben_item)) {
                bencode_int_value(&ben_item, &file->length);
                multi->total_length += file->length;
            }
            else if (strncmp(key, "md5sum", key_len) == 0 && bencode_is_string(&ben_item)) {
                const char *val;
                int val_len;
                bencode_string_value(&ben_item, &val, &val_len);
                file->md5sum = strndup(val, val_len);
            }
            else if (strncmp(key, "path", key_len) == 0 && bencode_is_list(&ben_item)) {
                file->path = join_path_components(&ben_item);
            }
        }
    }
}

static char* join_path_components(bencode_t *path_list) {
    char *path = NULL;
    size_t total_len = 0;
    bencode_t ben;
    bencode_clone(path_list, &ben);

    // 1st pass to calculate total length
    while (bencode_list_has_next(&ben)) {
        bencode_t component;
        bencode_list_get_next(&ben, &component);
        
        const char *val;
        int val_len;
        bencode_string_value(&component, &val, &val_len);
        total_len += val_len + 1; // +1 for separator
    }

    // alloc memory for path
    path = malloc(total_len);
    char *current = path;
    bencode_clone(path_list, &ben);

    int first = 1;
    while (bencode_list_has_next(&ben)) {
        bencode_t component;
        bencode_list_get_next(&ben, &component);
        
        const char *val;
        int val_len;
        bencode_string_value(&component, &val, &val_len);

        if (!first) {
            *current++ = '/';
        }
        first = 0;

        memcpy(current, val, val_len);
        current += val_len;
    }
    *current = '\0';

    return path;
}

static void calculate_info_hash(bencode_t *ben_item, Torrent *torrent) {
    const char *info;
    int info_len;
    bencode_t info_clone;

    bencode_clone(ben_item, &info_clone);

    if (bencode_dict_get_start_and_len(&info_clone, &info, &info_len) != 0) {
        fprintf(stderr, "Error: cannot extract raw info dictionary for hash\n");
        return;
    }

    struct sha1sum_ctx *ctx = sha1sum_create(NULL, 0);
    if (!ctx) {
		fprintf(stderr, "Error creating checksum\n");
		return;
	}

    uint8_t checksum[20];
    if (sha1sum_finish(ctx, (const uint8_t*)info, (size_t)info_len, checksum) != 0) {
        fprintf(stderr, "Error creating checksum\n");
        sha1sum_destroy(ctx);
        return;
    }

    sha1sum_destroy(ctx);
    memcpy(torrent->info_hash, checksum, 20);
}

// GETTERS
const char* torrent_get_announce(const Torrent* torrent) {
    return torrent ? torrent->announce : NULL;
}

const char* torrent_get_comment(const Torrent* torrent) {
    return torrent ? torrent->comment : NULL;
}

long torrent_get_piece_length(const Torrent* torrent) {
    return torrent ? torrent->info.piece_length : -1;
}

int torrent_get_piece_count(const Torrent* torrent) {
    return torrent ? torrent->info.pieces_count : -1;
}

const char* torrent_get_created_by(const Torrent* torrent) {
    return torrent ? torrent->created_by : NULL;
}

const char* torrent_get_encoding(const Torrent* torrent) {
    return torrent ? torrent->encoding : NULL;
}

const unsigned char *torrent_get_info_hash(const Torrent* torrent) {
    return torrent ? torrent->info_hash : NULL;
}

time_t torrent_get_creation_date(const Torrent* torrent) {
    return torrent ? torrent->creation_date : -1;
}