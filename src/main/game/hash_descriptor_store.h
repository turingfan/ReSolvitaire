#ifndef SOLVITAIRE_HASH_DESCRIPTOR_STORE_H
#define SOLVITAIRE_HASH_DESCRIPTOR_STORE_H

#include <cstdint>
#include <cstring>
#include "descriptor.h"

// Lightweight old-value store for incremental Zobrist hash maintenance in the
// hash-only cache path. Replaces compact_state in SOLVITAIRE_HASH_ONLY builds.
//
// Unlike compact_state, descriptors are stored as a plain byte array (one byte
// per card, no nibble packing). Getters and setters are direct array accesses
// with no bit operations. Foundation ranks, waste pointer, and hole top are
// also plain bytes.
//
// This struct is NOT a cache key and is never copied into cache clusters.
// It exists solely on the DFS stack as old-value storage for XOR delta
// computation in the four update_*_in_hash helpers.
//
// Note: 52 bytes for descriptors vs 26 bytes (nibble-packed) in compact_state.
// The extra 26 bytes per game_state on the DFS stack is negligible — the DFS
// stack is at most ~200 deep, giving ~5 KB extra vs millions of cache cluster
// copies in compact_state format. See known-issues.md #17 for a future
// optimisation that could apply the same byte-array approach to the flat cache.
struct hash_descriptor_store {
    uint8_t desc[52];       // descriptor per card; index = card_id (0-51)
    uint8_t foundations[4]; // top rank per suit (0 = empty)
    uint8_t waste_ptr;      // effective waste pointer (0-63)
    uint8_t hole_top;       // card_id of hole top (0 = none)

    void clear() {
        std::memset(desc, card_descriptor::STARTING, 52);
        std::memset(foundations, 0, 4);
        waste_ptr = 0;
        hole_top  = 0;
    }

    uint8_t get_descriptor(uint8_t card_id) const { return desc[card_id]; }
    void    set_descriptor(uint8_t card_id, uint8_t value) { desc[card_id] = value; }

    uint8_t get_foundation(uint8_t suit) const { return foundations[suit]; }
    void    set_foundation(uint8_t suit, uint8_t rank) { foundations[suit] = rank; }

    uint8_t get_waste_ptr() const { return waste_ptr; }
    void    set_waste_ptr(uint8_t ptr) { waste_ptr = ptr; }

    uint8_t get_hole_top() const { return hole_top; }
    void    set_hole_top(uint8_t cid) { hole_top = cid; }
};

#endif // SOLVITAIRE_HASH_DESCRIPTOR_STORE_H
