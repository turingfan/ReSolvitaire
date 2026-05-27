#ifndef SOLVITAIRE_MULTIPLICITY_STATIC_CLASS_H
#define SOLVITAIRE_MULTIPLICITY_STATIC_CLASS_H

// ─── multiplicity_static_class.h ────────────────────────────────────────────
//
// Static equivalence class structure for the multiplicity encoding (v4 spec §4).
//
// A static class groups cards that are interchangeable under the active suit
// symmetry.  The symmetry mode depends on the game rules and the active
// streamliner:
//
//   NONE            — 52 classes of 1 (Stage 1 behaviour, no canonicalisation)
//   COLOUR          — 26 classes of 2 (H↔D, S↔C; RED_BLACK build policy)
//   SUIT_IRRELEVANT — 13 classes of 4 (all suits per rank)
//
// Solvitaire suit encoding: Clubs=0 (black), Hearts=1 (red), Spades=2 (black),
// Diamonds=3 (red).  card_id = suit*13 + (rank-1).
//
// Note: `bool suit_sym` is used instead of `game_state::streamliner_options`
// to avoid a circular include dependency.  The caller computes the bool from
// stream_opts.
//
// Stage 2 of the multiplicity encoding implementation plan.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include "sol_rules.h"

// ─── Symmetry mode ───────────────────────────────────────────────────────────

enum class symmetry_mode : uint8_t { NONE, COLOUR, SUIT_IRRELEVANT };

// Determine the symmetry mode from rules and the suit-symmetry streamliner flag.
// Mirrors the canonicalisation logic in global_cache.cpp.

inline symmetry_mode determine_symmetry_mode(const sol_rules& rules, bool suit_sym) {
    if (!suit_sym) return symmetry_mode::NONE;
    if (rules.hole) return symmetry_mode::SUIT_IRRELEVANT;
    if (rules.build_pol == sol_rules::build_policy::RED_BLACK)
        return symmetry_mode::COLOUR;
    if (rules.build_pol == sol_rules::build_policy::SAME_SUIT)
        return symmetry_mode::NONE;  // suits structurally distinguishable
    return symmetry_mode::SUIT_IRRELEVANT;
}

// ─── Static class structure ───────────────────────────────────────────────────
//
// Describes the partition of all 52 cards into static equivalence classes.
//
// Members of each class are stored contiguously in class_members[]:
//   class c occupies class_members[class_start[c] .. class_start[c] + class_size - 1]
//
// class_of[card_id]    → which class this card belongs to
// class_start[cls]     → first index in class_members[] for class cls
// class_members[...]   → card IDs, ordered (initially by card_id within each class;
//                         reordered in-place by recompute_from_descriptors during sort)

struct static_class_structure {
    uint8_t n_classes;          // 13, 26, or 52
    uint8_t class_size;         // 4, 2, or 1
    uint8_t class_of[52];       // card_id → class_id
    uint8_t class_start[52];    // class_id → first index in class_members
    uint8_t class_members[52];  // card IDs, grouped by class

    void init(symmetry_mode mode) {
        switch (mode) {

        case symmetry_mode::NONE:
            n_classes = 52; class_size = 1;
            for (uint8_t i = 0; i < 52; i++) {
                class_of[i]      = i;
                class_start[i]   = i;
                class_members[i] = i;
            }
            break;

        case symmetry_mode::COLOUR:
            // 26 classes of 2: rank × colour.
            // colour 0 = black (Clubs=suit0, Spades=suit2)
            // colour 1 = red   (Hearts=suit1, Diamonds=suit3)
            // class_id = rank_idx*2 + colour  (0..25)
            n_classes = 26; class_size = 2;
            for (uint8_t cid = 0; cid < 52; cid++) {
                uint8_t suit     = cid / 13;
                uint8_t rank_idx = cid % 13;
                uint8_t colour   = (suit == 0 || suit == 2) ? 0 : 1;  // 0=black, 1=red
                class_of[cid] = rank_idx * 2 + colour;
            }
            for (uint8_t c = 0; c < 26; c++) class_start[c] = c * 2;
            // Fill class_members: for class c = rank_idx*2 + colour,
            //   colour 0 (black): Clubs(rank_idx), Spades(26+rank_idx)
            //   colour 1 (red):   Hearts(13+rank_idx), Diamonds(39+rank_idx)
            for (uint8_t c = 0; c < 26; c++) {
                uint8_t rank_idx = c / 2;
                uint8_t colour   = c % 2;
                if (colour == 0) {
                    class_members[c * 2]     = rank_idx;           // Clubs
                    class_members[c * 2 + 1] = 26 + rank_idx;     // Spades
                } else {
                    class_members[c * 2]     = 13 + rank_idx;     // Hearts
                    class_members[c * 2 + 1] = 39 + rank_idx;     // Diamonds
                }
            }
            break;

        case symmetry_mode::SUIT_IRRELEVANT:
            // 13 classes of 4: one per rank, all suits grouped.
            // class_id = rank_idx = card_id % 13  (0..12)
            n_classes = 13; class_size = 4;
            for (uint8_t cid = 0; cid < 52; cid++) {
                class_of[cid] = cid % 13;  // rank_idx
            }
            for (uint8_t c = 0; c < 13; c++) class_start[c] = c * 4;
            // Members: suits 0,1,2,3 for each rank_idx
            for (uint8_t c = 0; c < 13; c++) {
                for (uint8_t s = 0; s < 4; s++) {
                    class_members[c * 4 + s] = s * 13 + c;
                }
            }
            break;
        }
    }
};

#endif // SOLVITAIRE_MULTIPLICITY_STATIC_CLASS_H
