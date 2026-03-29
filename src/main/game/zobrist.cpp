#include "zobrist.h"
#include <random>

// Static member definitions
uint64_t zobrist_hash::Z_card[52][16];
uint64_t zobrist_hash::Z_found[4][14];
uint64_t zobrist_hash::Z_waste[64];
uint64_t zobrist_hash::Z_hole_top[52];
bool zobrist_hash::initialised = false;

void zobrist_hash::init(uint64_t seed) {
    if (initialised) return;

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint64_t> dist;

    // Fill Z_card[52][16]
    for (int c = 0; c < 52; ++c) {
        for (int d = 0; d < 16; ++d) {
            Z_card[c][d] = dist(rng);
        }
    }

    // Fill Z_found[4][14]
    for (int s = 0; s < 4; ++s) {
        for (int r = 0; r < 14; ++r) {
            Z_found[s][r] = dist(rng);
        }
    }

    // Fill Z_waste[64]
    for (int p = 0; p < 64; ++p) {
        Z_waste[p] = dist(rng);
    }

    // Fill Z_hole_top[52]
    for (int c = 0; c < 52; ++c) {
        Z_hole_top[c] = dist(rng);
    }

    initialised = true;
}

uint64_t zobrist_hash::card_key(uint8_t card_id, uint8_t descriptor) {
    if (card_id >= 52 || descriptor >= 16) return 0;
    return Z_card[card_id][descriptor];
}

uint64_t zobrist_hash::foundation_key(uint8_t suit, uint8_t rank) {
    if (suit >= 4 || rank >= 14) return 0;
    return Z_found[suit][rank];
}

uint64_t zobrist_hash::waste_key(uint8_t ptr) {
    if (ptr >= 64) return 0;
    return Z_waste[ptr];
}

uint64_t zobrist_hash::hole_top_key(uint8_t card_id) {
    if (card_id >= 52) return 0;
    return Z_hole_top[card_id];
}

uint8_t zobrist_hash::card_id(uint8_t suit, uint8_t rank) {
    // suit: 0-3, rank: 1-13
    return suit * 13 + (rank - 1);
}
