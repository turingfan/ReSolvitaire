/*
  Solvitaire: a solver for perfect information solitaire games
  Tests for Zobrist hash infrastructure (Milestone 2).
*/

#include <gtest/gtest.h>

#include "../test_helper.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/game/sol_rules.h"
#include "../../main/game/zobrist.h"

typedef std::initializer_list<std::initializer_list<std::string>> string_il;

// Helper: build a minimal FreeCell-like rules struct with tableau only
static sol_rules make_tableau_rules(int piles = 3) {
    sol_rules rules;
    rules.tableau_pile_count = static_cast<uint8_t>(piles);
    rules.build_pol = sol_rules::build_policy::SAME_SUIT;
    return rules;
}

// Test 1: Make a move and undo it — hash must return to original value.
TEST(Zobrist, MakeUndoHashInvariance) {
    zobrist_hash::init();
    sol_rules rules = make_tableau_rules(3);

    game_state gs(rules, string_il{{"AC", "2C"}, {"3D"}, {}});
    uint64_t hash_before = gs.get_zobrist_hash();

    // Make a regular move: take top card from pile 0, place on pile 2
    auto moves = gs.get_legal_moves();
    ASSERT_FALSE(moves.empty());

    for (auto& m : moves) {
        uint64_t h0 = gs.get_zobrist_hash();
        gs.make_move(m);
        gs.undo_move(m);
        EXPECT_EQ(gs.get_zobrist_hash(), h0)
            << "Hash not restored after make+undo of move";
    }

    EXPECT_EQ(gs.get_zobrist_hash(), hash_before);
}

// Test 2: Incremental hash matches from-scratch hash after a sequence of moves.
TEST(Zobrist, IncrementalVsFromScratch) {
    zobrist_hash::init();
    sol_rules rules = make_tableau_rules(4);

    game_state gs(rules, string_il{{"AC", "2C", "3C"}, {"4D", "5D"}, {"6H"}, {}});

    // Verify from the start
    uint64_t incremental = gs.get_zobrist_hash();
    gs.compute_hash_from_scratch();
    EXPECT_EQ(incremental, gs.get_zobrist_hash())
        << "Initial incremental hash doesn't match from-scratch";

    // Make a series of moves and check after each one
    for (int i = 0; i < 5; ++i) {
        auto moves = gs.get_legal_moves();
        if (moves.empty()) break;

        gs.make_move(moves.front());
        incremental = gs.get_zobrist_hash();

        gs.compute_hash_from_scratch();
        EXPECT_EQ(incremental, gs.get_zobrist_hash())
            << "Incremental hash diverged from scratch after move " << i;
    }
}

// Test 3: States differing only by swapping two interchangeable (tableau) piles
// must have identical Zobrist hashes.
TEST(Zobrist, SymmetryInvariance) {
    zobrist_hash::init();
    sol_rules rules = make_tableau_rules(3);

    // gs1: piles [AC], [2D], [3H]
    game_state gs1(rules, string_il{{"AC"}, {"2D"}, {"3H"}});
    // gs2: same cards, piles in different order [2D], [3H], [AC]
    game_state gs2(rules, string_il{{"2D"}, {"3H"}, {"AC"}});
    // gs3: different state — should have different hash
    game_state gs3(rules, string_il{{"AC"}, {"3D"}, {"3H"}});

    EXPECT_EQ(gs1.get_zobrist_hash(), gs2.get_zobrist_hash())
        << "Symmetry broken: permuted interchangeable piles have different hashes";
    EXPECT_NE(gs1.get_zobrist_hash(), gs3.get_zobrist_hash())
        << "Hash collision: different states have same hash";
}
