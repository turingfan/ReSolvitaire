#ifndef SOLVITAIRE_ZOBRIST_H
#define SOLVITAIRE_ZOBRIST_H

#include <cstdint>

struct sol_rules;
class game_state;

class zobrist_hash {
public:
    static void init(uint64_t seed = 0xDEADBEEF12345678ULL);

    // Per-card descriptor keys: Z_card[card_id][descriptor]
    static uint64_t card_key(uint8_t card_id, uint8_t descriptor);

    // Foundation top rank keys: Z_found[suit][rank] (rank 0=empty, 1=Ace, ..., 13=King)
    static uint64_t foundation_key(uint8_t suit, uint8_t rank);

    // Waste pointer keys: Z_waste[ptr] (ptr 0-63)
    static uint64_t waste_key(uint8_t ptr);

    // Hole top card keys: Z_hole_top[card_id] (for hole games, mutually exclusive with Z_found)
    static uint64_t hole_top_key(uint8_t card_id);

    // Card ID from suit and rank: suit * 13 + (rank - 1)
    static uint8_t card_id(uint8_t suit, uint8_t rank);

private:
    static uint64_t Z_card[52][16];     // 832 entries, ~6.5 KB
    static uint64_t Z_found[4][14];     // 56 entries
    static uint64_t Z_waste[64];        // 64 entries
    static uint64_t Z_hole_top[52];     // 52 entries
    static bool initialised;
};

#endif
