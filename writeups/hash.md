## Component: Hash.c

We need SHA-1 hashes fundamental in BitTorrent for verifying the integrity of downloaded pieces and identifying torrents (info hash).

- Usage Notes:

- Other components needing to compute or verify SHA-1 hashes (like torrent_parser for the info hash and piece_manager for piece verification) would use the functions provided:

- sha1sum_create() to start a hashing context, sha1sum_update() to add data incrementally, sha1sum_finish() to complete the hash calculation and get the result, sha1sum_reset() to reuse a context for a new hash, and sha1sum_destroy() to free resources. The sha1sum_truncated_head() function provides a way to get the first 8 bytes of a hash as a uint64_t.