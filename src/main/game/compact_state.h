#ifndef SOLVITAIRE_COMPACT_STATE_H
#define SOLVITAIRE_COMPACT_STATE_H

#include <cstdint>
#include <cstring>
#include "descriptor.h"

struct compact_state {
    uint8_t data[32];

    // Backward-compatibility aliases so existing flat-cache code can still write
    // compact_state::STARTING etc. without change.
    using descriptor = card_descriptor;
    static constexpr card_descriptor STARTING         = card_descriptor::STARTING;
    static constexpr card_descriptor STARTING_FACE_UP = card_descriptor::STARTING_FACE_UP;
    static constexpr card_descriptor ROOT             = card_descriptor::ROOT;
    static constexpr card_descriptor IN_CELL          = card_descriptor::IN_CELL;
    static constexpr card_descriptor PARENT_0         = card_descriptor::PARENT_0;
    static constexpr card_descriptor PARENT_1         = card_descriptor::PARENT_1;
    static constexpr card_descriptor PARENT_2         = card_descriptor::PARENT_2;
    static constexpr card_descriptor PARENT_3         = card_descriptor::PARENT_3;
    static constexpr card_descriptor IN_HOLE          = card_descriptor::IN_HOLE;
    static constexpr card_descriptor IN_SPACE         = card_descriptor::IN_SPACE;

    void clear();

    // Occupied flag (byte 0: 0 = empty slot, nonzero = occupied)
    void set_occupied(bool occ);
    bool is_occupied() const;

    // Depth counter (bytes 1-2, excluded from comparison)
    void set_depth(uint16_t d);
    uint16_t get_depth() const;

    // Foundation top ranks (bytes 3-4, 4 bits per suit)
    // Byte 3: Clubs (low nibble) | Hearts (high nibble)
    // Byte 4: Spades (low nibble) | Diamonds (high nibble)
    void set_foundation(uint8_t suit, uint8_t rank);
    uint8_t get_foundation(uint8_t suit) const;

    // Hole top card (byte 3, low 6 bits — for hole games, mutually exclusive with foundations)
    void set_hole_top(uint8_t card_id);
    uint8_t get_hole_top() const;

    // Waste pointer (byte 5, 0-63)
    void set_waste_ptr(uint8_t ptr);
    uint8_t get_waste_ptr() const;

    // Per-card descriptor (bytes 6-31, 4 bits per card, 52 cards)
    // Card c: byte_idx = 6 + (c / 2); even c = low nibble, odd c = high nibble
    void set_descriptor(uint8_t card_id, uint8_t value);
    uint8_t get_descriptor(uint8_t card_id) const;

    // Compare payloads (bytes 3-31 only, excluding occupied flag and depth)
    bool matches(const compact_state& other) const;
};

#endif
