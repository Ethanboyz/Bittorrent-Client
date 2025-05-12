#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>   // For ceil
#include <errno.h>  // For perror

#include "piece_manager.h"
#include "hash.h"       // For sha1sum functions
#include "btclient.h"   // For get_args() for debug mode

static ManagedPiece *all_managed_pieces = NULL;     // Array of all pieces
static uint32_t total_torrent_pieces = 0;           // Total pieces in torrent
static uint64_t total_torrent_file_length = 0;      // Total size of the file(s) to download
static uint32_t standard_piece_length = 0;          // Length of a standard piece

static uint8_t *client_bitfield = NULL;             // Our bitfield of HAVE pieces
static size_t client_bitfield_length_bytes = 0;     // Length of our bitfield

static FILE *output_file_ptr = NULL;                // File pointer for the download
static char *output_file_name_global = NULL;        // Name of the output file

static uint32_t pieces_we_have_count = 0;           // Count of pieces we have verified
static uint64_t bytes_we_have_downloaded = 0;       // Total verified bytes downloaded


static uint32_t calculate_num_blocks_for_piece(uint32_t piece_len_bytes);
static uint32_t calculate_block_length(uint32_t piece_actual_len, uint32_t block_index_in_piece, uint32_t num_total_blocks_for_this_piece);
static void set_bit_in_bitfield(uint8_t *bitfield_array, uint32_t piece_idx_to_set);
static bool get_bit_from_bitfield(const uint8_t *bitfield_array, uint32_t piece_idx_to_get, size_t bitfield_total_pieces_count);
static bool write_piece_data_to_file(uint32_t piece_idx_to_write, const uint8_t *data_to_write, uint32_t data_length);

int piece_manager_init(const Torrent *torrent, const char *output_filename) {
    if (!torrent || !output_filename) {
        if (get_args().debug_mode) fprintf(stderr, "[PieceManager] Error: Null torrent or output_filename to init.\n");
        return -1;
    }
    if (torrent->info.mode_type != MODE_SINGLE_FILE && torrent->info.mode_type != MODE_MULTI_FILE) {
        if (get_args().debug_mode) fprintf(stderr, "[PieceManager] Error: Torrent mode uninitialized.\n");
        return -1;
    }

    total_torrent_pieces = torrent_get_piece_count(torrent);
    standard_piece_length = torrent_get_piece_length(torrent);

    if (torrent->info.mode_type == MODE_SINGLE_FILE) {
        total_torrent_file_length = torrent->info.mode.single_file.length;
    } else { 
        total_torrent_file_length = torrent->info.mode.multi_file.total_length;
        if (get_args().debug_mode) {
            fprintf(stderr, "[PieceManager] Warning: Multi-file torrent. Treating as single concatenated file '%s'.\n", output_filename);
        }
    }

    if (total_torrent_pieces == 0 || standard_piece_length == 0 ) {
        if (get_args().debug_mode) fprintf(stderr, "[PieceManager] Error: Zero pieces or piece length in torrent.\n");
        return -1;
    }

    all_managed_pieces = calloc(total_torrent_pieces, sizeof(ManagedPiece));
    if (!all_managed_pieces) {
        if (get_args().debug_mode) perror("[PieceManager] Error alloc managed_pieces");
        return -1;
    }

    const unsigned char *torrent_piece_hashes_ptr = torrent->info.pieces;

    for (uint32_t i = 0; i < total_torrent_pieces; ++i) {
        all_managed_pieces[i].index = i;
        all_managed_pieces[i].state = PIECE_STATE_MISSING;
        all_managed_pieces[i].data_buffer = NULL;

        // Calculate actual length of this piece (last piece can be shorter)
        if (i == total_torrent_pieces - 1) {
            all_managed_pieces[i].piece_length = total_torrent_file_length - (standard_piece_length * (total_torrent_pieces - 1));
            if (total_torrent_file_length == 0 && total_torrent_pieces == 1) {
                 all_managed_pieces[i].piece_length = 0; 
            }
        } else {
            all_managed_pieces[i].piece_length = standard_piece_length;
        }
        
        if (torrent_piece_hashes_ptr) {
            memcpy(all_managed_pieces[i].expected_hash, torrent_piece_hashes_ptr + (i * 20), 20);
        } else {
             if (get_args().debug_mode) fprintf(stderr, "[PieceManager] Error: Torrent piece hashes are NULL.\n");
             free(all_managed_pieces); all_managed_pieces = NULL;
             return -1;
        }

        all_managed_pieces[i].num_total_blocks = calculate_num_blocks_for_piece(all_managed_pieces[i].piece_length);
        if (all_managed_pieces[i].num_total_blocks > 0) {
            all_managed_pieces[i].block_status_received = calloc(all_managed_pieces[i].num_total_blocks, sizeof(bool));
            if (!all_managed_pieces[i].block_status_received) {
                if (get_args().debug_mode) perror("[PieceManager] Error alloc block_status");
                for(uint32_t j=0; j<i; ++j) free(all_managed_pieces[j].block_status_received);
                free(all_managed_pieces); all_managed_pieces = NULL;
                return -1;
            }
        } else {
            all_managed_pieces[i].block_status_received = NULL;
        }
        all_managed_pieces[i].num_blocks_received = 0;
        all_managed_pieces[i].peer_availability_count = 0;
    }

    client_bitfield_length_bytes = (total_torrent_pieces + 7) / 8;
    if (client_bitfield_length_bytes > 0) {
        client_bitfield = calloc(client_bitfield_length_bytes, sizeof(uint8_t));
        if (!client_bitfield) {
            if (get_args().debug_mode) perror("[PieceManager] Error alloc client_bitfield");
            for(uint32_t i=0; i<total_torrent_pieces; ++i) free(all_managed_pieces[i].block_status_received);
            free(all_managed_pieces); all_managed_pieces = NULL;
            return -1;
        }
    } else {
        client_bitfield = NULL;
    }
    
    output_file_name_global = strdup(output_filename);
    if (!output_file_name_global) {
        if (get_args().debug_mode) perror("[PieceManager] Error strdup output_filename");
        if (client_bitfield) free(client_bitfield); client_bitfield = NULL;
        for(uint32_t i=0; i<total_torrent_pieces; ++i) free(all_managed_pieces[i].block_status_received);
        free(all_managed_pieces); all_managed_pieces = NULL;
        return -1;
    }

    // Open/create the output file
    output_file_ptr = fopen(output_file_name_global, "wb+");
    if (!output_file_ptr) {
        if (get_args().debug_mode) {
            fprintf(stderr, "[PieceManager] CRITICAL: Could not open/create file '%s': %s.\n",
                    output_file_name_global, strerror(errno));
        }
    } else {
        if (total_torrent_file_length > 0) {
            if (fseeko(output_file_ptr, total_torrent_file_length - 1, SEEK_SET) == 0) {
                if (fwrite("\0", 1, 1, output_file_ptr) != 1) {
                     if (get_args().debug_mode) fprintf(stderr, "[PieceManager] Warn: Pre-alloc write failed for '%s'.\n", output_file_name_global);
                }
                fflush(output_file_ptr);
                rewind(output_file_ptr);
            } else if (errno != 0) {
                 if (get_args().debug_mode) fprintf(stderr, "[PieceManager] Warn: Pre-alloc fseeko failed for '%s': %s\n", output_file_name_global, strerror(errno));
            }
        } else {
             fflush(output_file_ptr);
        }
    }
    
    pieces_we_have_count = 0;
    bytes_we_have_downloaded = 0;

    if (get_args().debug_mode) {
        fprintf(stderr, "[PieceManager] Initialized. Pieces: %u, File size: %lu, Output: %s\n",
                total_torrent_pieces, total_torrent_file_length, output_file_name_global);
    }
    return 0;
}

void piece_manager_destroy(void) {
    if (all_managed_pieces) {
        for (uint32_t i = 0; i < total_torrent_pieces; ++i) {
            free(all_managed_pieces[i].data_buffer);
            free(all_managed_pieces[i].block_status_received);
        }
        free(all_managed_pieces);
        all_managed_pieces = NULL;
    }
    free(client_bitfield);
    client_bitfield = NULL;

    if (output_file_ptr) {
        fclose(output_file_ptr);
        output_file_ptr = NULL;
    }
    free(output_file_name_global);
    output_file_name_global = NULL;

    // Reset counters
    total_torrent_pieces = 0;
    standard_piece_length = 0;
    total_torrent_file_length = 0;
    client_bitfield_length_bytes = 0;
    pieces_we_have_count = 0;
    bytes_we_have_downloaded = 0;
    if (get_args().debug_mode) fprintf(stderr, "[PieceManager] Destroyed.\n");
}

int piece_manager_record_block_received(uint32_t piece_index, uint32_t begin, const uint8_t *block_data, uint32_t block_length) {
    if (piece_index >= total_torrent_pieces || !all_managed_pieces) {
        // Invalid piece index
        return -1;
    }

    ManagedPiece *piece = &all_managed_pieces[piece_index];

    if (piece->state == PIECE_STATE_HAVE) return 0; // Already have, ignore
    if (block_length == 0 && piece->piece_length > 0) return 0; // Empty block for non-empty piece
    if (begin + block_length > piece->piece_length) return -1; // Block out of bounds

    uint32_t block_index_in_piece = (DEFAULT_BLOCK_LENGTH > 0) ? (begin / DEFAULT_BLOCK_LENGTH) : 0;
    if (DEFAULT_BLOCK_LENGTH == 0 && begin != 0 && piece->piece_length > 0) return -1;

    if (block_index_in_piece >= piece->num_total_blocks && piece->num_total_blocks > 0) return -1; // Invalid block index

    // Allocate piece data buffer if needed
    if (!piece->data_buffer && piece->piece_length > 0) {
        piece->data_buffer = malloc(piece->piece_length);
        if (!piece->data_buffer) return -1; // Malloc failed
    }
    
    if(piece->state == PIECE_STATE_MISSING) piece->state = PIECE_STATE_PENDING;

    // Copy block data
    if (piece->piece_length > 0 && piece->data_buffer) {
        memcpy(piece->data_buffer + begin, block_data, block_length);
    }

    // Update block received status
    if (piece->num_total_blocks > 0 && !piece->block_status_received[block_index_in_piece]) {
        piece->block_status_received[block_index_in_piece] = true;
        piece->num_blocks_received++;
    } else if (piece->num_total_blocks == 0 && piece->piece_length == 0 && piece->num_blocks_received == 0) {
        piece->num_blocks_received = 1; // Mark 0-byte piece as "complete"
    }

    // If piece is now complete, verify it
    if (piece_manager_is_piece_payload_complete(piece_index)) {
        if (!piece_manager_verify_and_write_piece(piece_index)) {
            // Verification failed, reset piece for re-download
            piece->state = PIECE_STATE_MISSING;
            piece->num_blocks_received = 0;
            if (piece->num_total_blocks > 0 && piece->block_status_received) {
                memset(piece->block_status_received, 0, piece->num_total_blocks * sizeof(bool));
            }
            return -1; // Indicate failure
        }
    }
    return 0;
}

bool piece_manager_is_piece_payload_complete(uint32_t piece_index) {
    if (piece_index >= total_torrent_pieces || !all_managed_pieces) return false;
    ManagedPiece *piece = &all_managed_pieces[piece_index];
    if (piece->piece_length == 0) return piece->num_blocks_received > 0 || piece->num_total_blocks == 0;
    return piece->num_blocks_received == piece->num_total_blocks;
}

bool piece_manager_verify_and_write_piece(uint32_t piece_index) {
    if (piece_index >= total_torrent_pieces || !all_managed_pieces) return false;
    ManagedPiece *piece = &all_managed_pieces[piece_index];

    if (piece->state == PIECE_STATE_HAVE) return true; // Already verified
    if (piece->state != PIECE_STATE_PENDING || !piece_manager_is_piece_payload_complete(piece_index)) return false; // Not ready
    if (!piece->data_buffer && piece->piece_length > 0) return false; // No data to verify

    uint8_t calculated_hash[20];
    struct sha1sum_ctx *ctx = sha1sum_create(NULL, 0);
    if (!ctx) return false; // Hash context creation failed

    const uint8_t* data_for_hash = piece->piece_length > 0 ? piece->data_buffer : NULL;
    if (sha1sum_finish(ctx, data_for_hash, piece->piece_length, calculated_hash) != 0) {
        sha1sum_destroy(ctx);
        return false; // Hash calculation failed
    }
    sha1sum_destroy(ctx);

    if (memcmp(calculated_hash, piece->expected_hash, 20) == 0) {
        // Hash matches
        if (piece->piece_length > 0) {
            if (!write_piece_data_to_file(piece_index, piece->data_buffer, piece->piece_length)) {
                return false; // File write failed
            }
        }

        piece->state = PIECE_STATE_HAVE;
        if(client_bitfield) set_bit_in_bitfield(client_bitfield, piece_index);
        pieces_we_have_count++;
        bytes_we_have_downloaded += piece->piece_length;

        free(piece->data_buffer); // Free memory after successful write
        piece->data_buffer = NULL;
        
        if (get_args().debug_mode && piece_manager_is_download_complete()) {
             fprintf(stderr, "[PieceManager] ****** DOWNLOAD COMPLETE! ******\n");
        }
        return true;
    } else {
        // Hash mismatch
        if (get_args().debug_mode) fprintf(stderr, "[PieceManager] Piece %u VERIFICATION FAILED.\n", piece_index);
        return false;
    }
}

bool piece_manager_select_piece_for_peer(const uint8_t *peer_bitfield, size_t peer_bitfield_len_bytes, uint32_t *selected_piece_index) {
    if (!peer_bitfield || !selected_piece_index || !all_managed_pieces) return false;

    size_t peer_total_pieces = peer_bitfield_len_bytes * 8;
    // Simple sequential scan for a piece the peer has and we need
    for (uint32_t i = 0; i < total_torrent_pieces; ++i) {
        if (all_managed_pieces[i].state == PIECE_STATE_MISSING) {
            if (get_bit_from_bitfield(peer_bitfield, i, peer_total_pieces)) {
                *selected_piece_index = i;
                return true;
            }
        }
    }
    return false; // No suitable piece found
}

bool piece_manager_get_block_to_request_from_piece(uint32_t piece_idx, uint32_t *begin_out, uint32_t *length_out) {
    if (piece_idx >= total_torrent_pieces || !all_managed_pieces || !begin_out || !length_out) return false;

    ManagedPiece *piece = &all_managed_pieces[piece_idx];

    // Transition from MISSING to PENDING
    if (piece->state == PIECE_STATE_MISSING) {
        if (!piece->data_buffer && piece->piece_length > 0) {
             piece->data_buffer = malloc(piece->piece_length);
             if (!piece->data_buffer) return false; // Malloc failed
        }
        piece->state = PIECE_STATE_PENDING;
    }

    if (piece->state != PIECE_STATE_PENDING) return false; // Not in a state to request blocks
    if (piece->piece_length == 0 && piece->num_total_blocks == 0) return false; // 0-byte piece

    // Find first unreceived block
    for (uint32_t block_i = 0; block_i < piece->num_total_blocks; ++block_i) {
        if (!piece->block_status_received[block_i]) {
            *begin_out = block_i * DEFAULT_BLOCK_LENGTH;
            *length_out = calculate_block_length(piece->piece_length, block_i, piece->num_total_blocks);
            return true;
        }
    }
    return false; // All blocks for this PENDING piece are already marked received (should be HAVE soon)
}

void piece_manager_get_our_bitfield(const uint8_t **bitfield_out, size_t *length_out) {
    if (bitfield_out) *bitfield_out = client_bitfield;
    if (length_out) *length_out = client_bitfield_length_bytes;
}

void piece_manager_update_peer_availability(uint32_t piece_index, bool peer_has_it) {
    if (piece_index >= total_torrent_pieces || !all_managed_pieces) return;
    // Used for rarest-first strategy
    if (peer_has_it) {
        all_managed_pieces[piece_index].peer_availability_count++;
    } else {
        if (all_managed_pieces[piece_index].peer_availability_count > 0) {
            all_managed_pieces[piece_index].peer_availability_count--;
        }
    }
}

bool piece_manager_is_download_complete(void) {
    if (!all_managed_pieces || total_torrent_pieces == 0) {
        return total_torrent_file_length == 0; // Empty file is complete
    }
    return pieces_we_have_count == total_torrent_pieces;
}

PieceState piece_manager_get_piece_state(uint32_t piece_index) {
    if (piece_index >= total_torrent_pieces || !all_managed_pieces) return PIECE_STATE_MISSING;
    return all_managed_pieces[piece_index].state;
}

uint32_t piece_manager_get_total_pieces_count(void) {
    return total_torrent_pieces;
}

uint64_t piece_manager_get_bytes_downloaded_total(void) {
    return bytes_we_have_downloaded;
}

uint64_t piece_manager_get_bytes_left_total(void) {
    if (total_torrent_file_length < bytes_we_have_downloaded) return 0;
    return total_torrent_file_length - bytes_we_have_downloaded;
}

bool piece_manager_has_block(uint32_t piece_index, uint32_t block_offset) {
    if (piece_index >= total_torrent_pieces || !all_managed_pieces) return false;
    ManagedPiece *piece = &all_managed_pieces[piece_index];
    if (block_offset >= piece->piece_length) return false; 
    if (DEFAULT_BLOCK_LENGTH == 0) return piece->num_total_blocks > 0 && piece->block_status_received[0];

    uint32_t block_index_in_piece = block_offset / DEFAULT_BLOCK_LENGTH;
    if (block_index_in_piece >= piece->num_total_blocks) return false;
    return piece->block_status_received[block_index_in_piece];
}

bool piece_manager_read_block(uint32_t piece_index, uint32_t begin, uint32_t block_length, uint8_t *block) {
    ManagedPiece *piece = &all_managed_pieces[piece_index];

    if (block_length == 0 && piece->piece_length > 0) return true; // Empty block for non-empty piece
    if (begin + block_length > piece->piece_length) return false; // Block out of bounds

    uint32_t block_index_in_piece = (DEFAULT_BLOCK_LENGTH > 0) ? (begin / DEFAULT_BLOCK_LENGTH) : 0;
    if (block_index_in_piece >= piece->num_total_blocks && piece->num_total_blocks > 0) return false; // Invalid block index

    // calculate file offset to where block is
    uint64_t file_offset = (uint64_t)piece_index * standard_piece_length + begin;
    if (fseeko(output_file_ptr, file_offset, SEEK_SET) < 0) {
        return false;
    }

    // read block data from file into buffer 
    if (fread(block, 1, block_length, output_file_ptr) != block_length) {
        return false;
    }
    
    return true;
}

// --- Helper Function Implementations ---
static uint32_t calculate_num_blocks_for_piece(uint32_t piece_len_bytes) {
    if (piece_len_bytes == 0) return 0;
    if (DEFAULT_BLOCK_LENGTH == 0) return (piece_len_bytes > 0) ? 1 : 0;
    return (uint32_t)ceil((double)piece_len_bytes / DEFAULT_BLOCK_LENGTH);
}

static uint32_t calculate_block_length(uint32_t piece_actual_len, uint32_t block_index_in_piece, uint32_t num_total_blocks_for_this_piece) {
    if (piece_actual_len == 0 || num_total_blocks_for_this_piece == 0) return 0;
    // Standard block length, unless it's the last block of the piece
    if (block_index_in_piece < num_total_blocks_for_this_piece - 1) {
        return DEFAULT_BLOCK_LENGTH;
    } else {
        // Last block length calculation
        return piece_actual_len - (block_index_in_piece * DEFAULT_BLOCK_LENGTH);
    }
}

static void set_bit_in_bitfield(uint8_t *bitfield_array, uint32_t piece_idx_to_set) {
    if (!bitfield_array || piece_idx_to_set >= (client_bitfield_length_bytes * 8) ) return; // Boundary check
    uint32_t byte_index = piece_idx_to_set / 8;
    uint8_t bit_offset_in_byte = 7 - (piece_idx_to_set % 8); // MSB is piece 0
    bitfield_array[byte_index] |= (1 << bit_offset_in_byte);
}

static bool get_bit_from_bitfield(const uint8_t *bitfield_array, uint32_t piece_idx_to_get, size_t bitfield_total_pieces_count) {
    if (!bitfield_array || piece_idx_to_get >= bitfield_total_pieces_count) return false; // Boundary check
    uint32_t byte_index = piece_idx_to_get / 8;
    uint8_t bit_offset_in_byte = 7 - (piece_idx_to_get % 8);
    return (bitfield_array[byte_index] >> bit_offset_in_byte) & 1;
}

static bool write_piece_data_to_file(uint32_t piece_idx_to_write, const uint8_t *data_to_write, uint32_t data_length) {
    if (!output_file_ptr) return false; // File not open
    if (!data_to_write || data_length == 0) return true; // Nothing to write for 0-length piece

    uint64_t file_offset = (uint64_t)piece_idx_to_write * standard_piece_length;

    if (fseeko(output_file_ptr, file_offset, SEEK_SET) != 0) return false; // Seek failed
    if (fwrite(data_to_write, 1, data_length, output_file_ptr) != data_length) return false; // Write failed
    
    fflush(output_file_ptr); // Ensure data is on disk
    return true;
}