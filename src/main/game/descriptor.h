#ifndef SOLVITAIRE_DESCRIPTOR_H
#define SOLVITAIRE_DESCRIPTOR_H

#include <cstdint>

// Per-card descriptor values used by the Zobrist hash and flat cache payload.
// Shared between compact_state (flat-cache path) and hash_descriptor_store
// (hash-only path) so neither path needs to include the other's header.
enum card_descriptor : uint8_t {
    STARTING         = 0,  // Face-down in original position; also reused for foundation cards
    STARTING_FACE_UP = 1,  // Originally face-down, now revealed, not yet moved, not at pile bottom
    ROOT             = 2,  // At pile bottom with no legal-build parent (parent_table fallback)
    IN_CELL          = 3,  // In a free cell
    PARENT_0         = 4,  // Built on first legal parent (fixed suit ordering)
    PARENT_1         = 5,  // Built on second legal parent
    PARENT_2         = 6,  // Built on third legal parent
    PARENT_3         = 7,  // Built on fourth legal parent
    IN_HOLE          = 8,  // Played to the hole (hole games only)
    IN_SPACE         = 9,  // At the bottom of a tableau pile (nothing below, or only face-down)
    // 10-15 reserved
};

#endif // SOLVITAIRE_DESCRIPTOR_H
