#ifndef SOLVITAIRE_COMPACT_STATE_H
#define SOLVITAIRE_COMPACT_STATE_H

#include <cstdint>
#include <cstring>

struct compact_state {
    uint8_t data[32];

    enum descriptor : uint8_t {
        STARTING         = 0,
        STARTING_FACE_UP = 1,
        ROOT             = 2,
        IN_CELL          = 3,
        PARENT_0         = 4,
        PARENT_1         = 5,
        PARENT_2         = 6,
        PARENT_3         = 7,
        IN_HOLE          = 8,
    };

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
