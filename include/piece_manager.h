#ifndef PIECE_MANAGER_H
#define PIECE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "torrent_parser.h" // For Torrent struct
#include "peer_manager.h"   // For Peer's bitfield context (optional here)

#define DEFAULT_BLOCK_LENGTH 16384 // 16 KiB, common block request size

// Represents the client's state regarding a piece
typedef enum {
    PIECE_STATE_MISSING,    // Don't have, not requested
    PIECE_STATE_PENDING,    // Some/all blocks requested, not yet verified
    PIECE_STATE_HAVE        // All blocks received and verified
} PieceState;

// Internal tracking for each piece of the torrent
typedef struct {
    uint32_t index;                 // Piece index (0 to N-1)
    PieceState state;               // Current download state of this piece
    uint8_t *data_buffer;           // Buffer for incoming block data
    uint32_t piece_length;          // Actual byte length of this piece
    uint8_t expected_hash[20];      // SHA-1 hash from .torrent metafile

    uint32_t num_total_blocks;      // How many blocks make up this piece
    bool *block_status_received;    // Tracks received blocks for this piece
    uint32_t num_blocks_received;   // Count of blocks successfully received
    bool *block_requested;

    // For rarest-first strategy (extra credit)
    int peer_availability_count;  // How many connected peers have this piece
} ManagedPiece;

/**
 * @brief Initialize piece manager with torrent data and output file.
 * @param torrent Parsed torrent file metadata.
 * @param output_filename Filename for the downloaded content.
 * @return 0 on success, -1 on failure.
 */
int piece_manager_init(const Torrent *torrent, const char *output_filename);

/**
 * @brief Clean up resources used by the piece manager.
 */
void piece_manager_destroy(void);

/**
 * @brief Record a received block of data for a piece.
 * @param piece_index Index of the piece.
 * @param begin Byte offset within the piece.
 * @param block_data Pointer to the block's data.
 * @param block_length Length of the block's data.
 * @return 0 on success, -1 on error or verification failure.
 */
int piece_manager_record_block_received(uint32_t piece_index, uint32_t begin, const uint8_t *block_data, uint32_t block_length);

/**
 * @brief Check if all blocks for a piece have been received.
 * @param piece_index Index of the piece.
 * @return true if all blocks received, false otherwise.
 */
bool piece_manager_is_piece_payload_complete(uint32_t piece_index);

/**
 * @brief Verify a completed piece against its SHA-1 hash and write to file.
 * @param piece_index Index of the piece to verify.
 * @return true if verified and processed, false otherwise.
 */
bool piece_manager_verify_and_write_piece(uint32_t piece_index);

/**
 * @brief Select a piece that a peer has and we need.
 * @param peer_bitfield Peer's bitfield of available pieces.
 * @param peer_bitfield_len_bytes Length of peer's bitfield.
 * @param selected_piece_index Output for the selected piece's index.
 * @return true if a piece is selected, false otherwise.
 */
bool piece_manager_select_piece_for_peer(const uint8_t *peer_bitfield, size_t peer_bitfield_len_bytes, uint32_t *selected_piece_index);

/**
 * @brief Get the next block to request from a specific piece.
 * @param piece_idx Index of the piece.
 * @param begin_out Output for the block's starting offset.
 * @param length_out Output for the block's length.
 * @return true if a block is found, false otherwise.
 */
bool piece_manager_get_block_to_request_from_piece(uint32_t piece_idx, uint32_t *begin_out, uint32_t *length_out);

/**
 * @brief Get the client's current bitfield of verified pieces.
 * @param bitfield Output for pointer to the bitfield.
 * @param length Output for the bitfield's length in bytes.
 */
void piece_manager_get_our_bitfield(const uint8_t **bitfield, size_t *length);

/**
 * @brief Update peer availability count for a piece (for rarest-first).
 * @param piece_index Index of the piece.
 * @param peer_has_it true if peer has it, false otherwise.
 */
void piece_manager_update_peer_availability(uint32_t piece_index, bool peer_has_it);

/**
 * @brief Check if the entire torrent download is complete.
 * @return true if all pieces are HAVE, false otherwise.
 */
bool piece_manager_is_download_complete(void);

/**
 * @brief Get the current state of a piece.
 * @param piece_index Index of the piece.
 * @return PieceState of the piece.
 */
PieceState piece_manager_get_piece_state(uint32_t piece_index);

/**
 * @brief Get total number of pieces in the torrent.
 * @return Total piece count.
 */
uint32_t piece_manager_get_total_pieces_count(void);

/**
 * @brief Get total bytes downloaded and verified.
 * @return Count of downloaded bytes.
 */
uint64_t piece_manager_get_bytes_downloaded_total(void);

/**
 * @brief Get total bytes remaining to download.
 * @return Count of bytes left.
 */
uint64_t piece_manager_get_bytes_left_total(void);

/**
 * @brief Check if a specific block has been received.
 * @param piece_index Index of the piece.
 * @param block_offset Starting byte offset of the block.
 * @return true if block received, false otherwise.
 */
bool piece_manager_has_block(uint32_t piece_index, uint32_t block_offset);

/**
 * @brief Read a block from the file into block buffer parameter.
 * @param piece_idx Index of the piece from which block is needed.
 * @param begin Byte offset within the piece.
 * @param block_length Length of the block's data.
 * @param block Buffer for block data read.
 * @return true if block read, false otherwise.
 */
bool piece_manager_read_block(uint32_t piece_index, uint32_t begin, uint32_t block_length, uint8_t *block);

#endif // PIECE_MANAGER_H