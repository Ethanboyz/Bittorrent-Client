/**
 * This file provides functions to allow us to parse and
 * display information from a BitTorrent metainfo file (.torrent)
 */

#ifndef TORRENT_PARSER_H
#define TORRENT_PARSER_H

#include <time.h>
#include "bencode.h"

/*
 * BELOW ARE THE RELEVANT DATA STRUCTURES OF A TORRENT FILE -- based on what i was able to understand from the wiki
 */

typedef enum {
    MODE_UNINITIALIZED,
    MODE_SINGLE_FILE,
    MODE_MULTI_FILE
} ModeType;

typedef struct {
    char *path;
    long length;
    char *md5sum;
} TorrentFile;

typedef struct {
    TorrentFile *files;
    int files_count;
    long total_length;
} MultiFileInfo;

typedef struct {
    long length;
    char *md5sum;
} SingleFileInfo;

typedef struct {
    char *name;
    long piece_length;
    unsigned char *pieces;
    int pieces_count;
    int is_private;
    ModeType mode_type;
    union {
        SingleFileInfo single_file;
        MultiFileInfo multi_file;
    } mode;
} TorrentInfo;

typedef struct {
    char *announce;
    char **announce_list;
    int announce_list_count;
    time_t creation_date;
    char *comment;
    char *created_by;
    char *encoding;
    TorrentInfo info;
    unsigned char info_hash[20];
} Torrent;

// lifecycle of a Torrent object
Torrent *torrent_create(void);
void torrent_free(Torrent *torrent);

// the main function to parse a torrent file
int parse_torrent_file(const char *torrent_data, int len, Torrent **out_torrent);

// aux function that just reads and ensures a torrent file can be open/read
int read_torrent_file(const char *filename, char *buffer, int buffer_size);

// Getters -- add more as needed???
const char *torrent_get_announce(const Torrent *torrent);
const char *torrent_get_comment(const Torrent *torrent);
long torrent_get_piece_length(const Torrent *torrent);
int torrent_get_piece_count(const Torrent *torrent);
time_t torrent_get_creation_date(const Torrent *torrent);
const char *torrent_get_created_by(const Torrent *torrent);
const char *torrent_get_encoding(const Torrent *torrent);
const unsigned char *torrent_get_info_hash(const Torrent *torrent);

#endif