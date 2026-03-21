#include "zobrist.h"

// Static member definitions
std::array<std::array<std::array<uint64_t, zobrist_hash::MAX_PILE_SIZE>,
                      static_cast<int>(zobrist_hash::pile_role::NUM_ROLES)>,
           zobrist_hash::NUM_CARDS> zobrist_hash::key_table;
bool zobrist_hash::initialised = false;

void zobrist_hash::init(uint64_t seed) {
    if (initialised) return;

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint64_t> dist;

    for (int c = 0; c < NUM_CARDS; ++c) {
        for (int r = 0; r < static_cast<int>(pile_role::NUM_ROLES); ++r) {
            for (int p = 0; p < MAX_PILE_SIZE; ++p) {
                key_table[c][r][p] = dist(rng);
            }
        }
    }

    initialised = true;
}

uint64_t zobrist_hash::key(uint8_t card_id, pile_role role, uint8_t position) {
    if (card_id >= NUM_CARDS) return 0;
    if (position >= MAX_PILE_SIZE) return 0;

    return key_table[card_id][static_cast<int>(role)][position];
}

uint8_t zobrist_hash::card_id(uint8_t suit, uint8_t rank) {
    // suit: 0-3, rank: 1-13
    return suit * 13 + (rank - 1);
}
