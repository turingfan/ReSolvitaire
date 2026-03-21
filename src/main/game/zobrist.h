#ifndef SOLVITAIRE_ZOBRIST_H
#define SOLVITAIRE_ZOBRIST_H

#include <cstdint>
#include <array>
#include <random>

// Zobrist hash infrastructure for perfect-information solitaire.
//
// Zobrist hashing encodes game state as a 64-bit hash via XOR of random keys.
// Each card in a specific role and position gets a unique random key. Moving a card
// updates the hash incrementally without recomputing the full state.
//
// For symmetry invariance (important for games with interchangeable piles like FreeCell):
// - Per-pile hash: XOR of all cards in that pile
// - Global hash for non-interchangeable piles: XOR of per-pile hashes
// - Global hash for interchangeable piles: SUM of per-pile hashes (mod 2^64)
//
// This ensures states that differ only by reordering interchangeable piles have the same hash.

struct sol_rules;
class game_state;

class zobrist_hash {
public:
    // Maximum pile positions we support (two-deck solitaire max)
    static constexpr int MAX_PILE_SIZE = 104;
    // Standard deck: 52 cards
    static constexpr int NUM_CARDS = 52;

    enum class pile_role : uint8_t {
        FOUNDATION = 0,
        TABLEAU = 1,
        STOCK = 2,
        WASTE = 3,
        RESERVE = 4,
        CELL = 5,
        HOLE = 6,
        NUM_ROLES = 7
    };

    // Initialize random key table with a fixed seed (reproducible)
    static void init(uint64_t seed = 0xDEADBEEF12345678ULL);

    // Look up the Zobrist key for a card in a specific role and position
    static uint64_t key(uint8_t card_id, pile_role role, uint8_t position);

    // Card ID from suit and rank
    // card_id = suit * 13 + (rank - 1)
    // Suit: 0=Clubs, 1=Hearts, 2=Spades, 3=Diamonds
    // Rank: 1-13 (Ace through King)
    static uint8_t card_id(uint8_t suit, uint8_t rank);

private:
    // key_table[card_id][role][position]
    static std::array<std::array<std::array<uint64_t, MAX_PILE_SIZE>,
                                  static_cast<int>(pile_role::NUM_ROLES)>,
                      NUM_CARDS> key_table;
    static bool initialised;
};

#endif
