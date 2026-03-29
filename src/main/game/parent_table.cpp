#include "parent_table.h"

std::vector<uint8_t> parent_table::get_parents(uint8_t card_id, sol_rules::build_policy policy,
                                               uint8_t foundations_base, uint8_t max_rank) {
    std::vector<uint8_t> parents;

    if (policy == sol_rules::build_policy::NO_BUILD) {
        return parents;
    }

    // Extract suit and rank from card_id
    // card_id = suit * 13 + (rank - 1)
    // suit = card_id / 13, rank = (card_id % 13) + 1
    uint8_t suit = card_id / 13;
    uint8_t rank = (card_id % 13) + 1;

    // Convert rank to position in the build sequence using foundation_base_convert.
    // Position 1 = foundation base rank (lowest), position max_rank = top of sequence (no parent).
    // For standard games (foundations_base=1), this is an identity: rank 1→1, ..., 13→13.
    // For canfield with e.g. base=5: rank 5→1, 6→2, ..., K→9, A→10, ..., 4→13.
    uint8_t converted = (rank - (foundations_base - 1) + max_rank) % max_rank;
    if (converted == 0) converted = max_rank;

    // Top of build sequence has no parent (equivalent to King in standard games)
    if (converted == max_rank) {
        return parents;
    }

    // Parent is one position higher in the build sequence
    uint8_t parent_converted = converted + 1;
    // Convert back to actual rank
    uint8_t parent_rank = ((parent_converted % max_rank) + foundations_base - 1) % max_rank;
    if (parent_rank == 0) parent_rank = max_rank;

    if (policy == sol_rules::build_policy::SAME_SUIT) {
        // Parent is same suit, next rank up in build sequence
        uint8_t parent_id = suit * 13 + (parent_rank - 1);
        parents.push_back(parent_id);
    } else if (policy == sol_rules::build_policy::RED_BLACK) {
        // Parents are opposite-colour, next rank up, ordered by parent suit index
        // Black suits: Clubs (0), Spades (2)
        // Red suits: Hearts (1), Diamonds (3)
        std::vector<uint8_t> candidate_suits;
        if (suit == 0 || suit == 2) {
            // Current is black, parents are red
            candidate_suits = {1, 3};  // Hearts, Diamonds
        } else {
            // Current is red, parents are black
            candidate_suits = {0, 2};  // Clubs, Spades
        }
        for (uint8_t p_suit : candidate_suits) {
            uint8_t parent_id = p_suit * 13 + (parent_rank - 1);
            parents.push_back(parent_id);
        }
    } else if (policy == sol_rules::build_policy::ANY_SUIT) {
        // Parents are all four suits, next rank up, ordered Clubs/Hearts/Spades/Diamonds
        for (uint8_t p_suit = 0; p_suit < 4; ++p_suit) {
            uint8_t parent_id = p_suit * 13 + (parent_rank - 1);
            parents.push_back(parent_id);
        }
    }

    return parents;
}

uint8_t parent_table::get_descriptor_for_parent(uint8_t card_id, uint8_t parent_card_id,
                                                sol_rules::build_policy policy,
                                                uint8_t foundations_base, uint8_t max_rank) {
    auto parents = get_parents(card_id, policy, foundations_base, max_rank);

    for (uint8_t i = 0; i < parents.size(); ++i) {
        if (parents[i] == parent_card_id) {
            // Return PARENT_0 through PARENT_3 (descriptor values 4-7)
            return static_cast<uint8_t>(4 + i);
        }
    }

    // Parent not found
    return 0;
}
