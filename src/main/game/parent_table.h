#ifndef SOLVITAIRE_PARENT_TABLE_H
#define SOLVITAIRE_PARENT_TABLE_H

#include <cstdint>
#include <vector>
#include "sol_rules.h"

class parent_table {
public:
    // Get list of parent card IDs for a given card under a specific build policy.
    // Card must be able to be built on one of the parents (rank r+1 in a compatible suit).
    // Returns empty vector if card is a King or build_policy is NO_BUILD.
    static std::vector<uint8_t> get_parents(uint8_t card_id, sol_rules::build_policy policy);

    // Get the descriptor value (PARENT_0 through PARENT_3) for a card built on a specific parent.
    // Returns 0 if invalid or no such parent exists for this card.
    static uint8_t get_descriptor_for_parent(uint8_t card_id, uint8_t parent_card_id, sol_rules::build_policy policy);
};

#endif
