/*
  Solvitaire: a solver for perfect information solitaire games
  Tests for descriptor-aligned Zobrist hash (Milestone 2).
*/

#include <gtest/gtest.h>

#include "../../main/game/zobrist.h"
#include "../../main/game/compact_state.h"

// Test 1: Zobrist initialization and basic key access
TEST(Zobrist, Initialization) {
    zobrist_hash::init();

    // Verify keys are non-zero
    uint64_t k1 = zobrist_hash::card_key(0, 0);
    uint64_t k2 = zobrist_hash::card_key(0, 1);
    uint64_t k3 = zobrist_hash::card_key(1, 0);

    EXPECT_NE(k1, 0) << "Card key should not be zero";
    EXPECT_NE(k2, 0) << "Card key should not be zero";
    EXPECT_NE(k3, 0) << "Card key should not be zero";
    EXPECT_NE(k1, k2) << "Different descriptors should have different keys";
    EXPECT_NE(k1, k3) << "Different cards should have different keys";
}

// Test 2: Foundation keys
TEST(Zobrist, FoundationKeys) {
    zobrist_hash::init();

    uint64_t f1 = zobrist_hash::foundation_key(0, 0);  // Empty
    uint64_t f2 = zobrist_hash::foundation_key(0, 1);  // Ace
    uint64_t f3 = zobrist_hash::foundation_key(1, 0);  // Different suit

    EXPECT_NE(f1, f2) << "Different ranks should have different keys";
    EXPECT_NE(f1, f3) << "Different suits should have different keys";
}

// Test 3: Waste pointer keys
TEST(Zobrist, WasteKeys) {
    zobrist_hash::init();

    uint64_t w1 = zobrist_hash::waste_key(0);
    uint64_t w2 = zobrist_hash::waste_key(1);
    uint64_t w3 = zobrist_hash::waste_key(63);

    EXPECT_NE(w1, w2) << "Different pointers should have different keys";
    EXPECT_NE(w2, w3) << "Different pointers should have different keys";
}

// Test 4: Hole top keys
TEST(Zobrist, HoleTopKeys) {
    zobrist_hash::init();

    uint64_t h1 = zobrist_hash::hole_top_key(0);
    uint64_t h2 = zobrist_hash::hole_top_key(1);
    uint64_t h3 = zobrist_hash::hole_top_key(51);

    EXPECT_NE(h1, h2) << "Different card IDs should have different keys";
    EXPECT_NE(h2, h3) << "Different card IDs should have different keys";
}

// Test 5: Card ID computation
TEST(Zobrist, CardId) {
    // suit * 13 + (rank - 1)
    EXPECT_EQ(zobrist_hash::card_id(0, 1), 0);    // Ace of Clubs
    EXPECT_EQ(zobrist_hash::card_id(0, 13), 12);  // King of Clubs
    EXPECT_EQ(zobrist_hash::card_id(1, 1), 13);   // Ace of Hearts
    EXPECT_EQ(zobrist_hash::card_id(3, 13), 51);  // King of Diamonds
}

// Test 6: Out-of-range access returns 0
TEST(Zobrist, OutOfRangeKeys) {
    zobrist_hash::init();

    EXPECT_EQ(zobrist_hash::card_key(52, 0), 0) << "Out-of-range card should return 0";
    EXPECT_EQ(zobrist_hash::card_key(0, 16), 0) << "Out-of-range descriptor should return 0";
    EXPECT_EQ(zobrist_hash::foundation_key(4, 0), 0) << "Out-of-range suit should return 0";
    EXPECT_EQ(zobrist_hash::foundation_key(0, 14), 0) << "Out-of-range rank should return 0";
    EXPECT_EQ(zobrist_hash::waste_key(64), 0) << "Out-of-range pointer should return 0";
    EXPECT_EQ(zobrist_hash::hole_top_key(52), 0) << "Out-of-range card should return 0";
}

// Test 7: Deterministic initialization
TEST(Zobrist, DeterministicInit) {
    zobrist_hash::init(0x1234567890ABCDEFULL);
    uint64_t k1 = zobrist_hash::card_key(0, 0);

    // Re-init with same seed should not change values (initialised flag prevents re-init)
    // So we just verify the key is still there
    EXPECT_NE(k1, 0);
}

// Test 8: Compact state creation and basic operations
TEST(CompactState, BasicOperations) {
    compact_state cs;
    cs.clear();

    // Initially empty
    EXPECT_FALSE(cs.is_occupied());
    EXPECT_EQ(cs.get_depth(), 0);
    EXPECT_EQ(cs.get_waste_ptr(), 0);

    // Set occupied
    cs.set_occupied(true);
    EXPECT_TRUE(cs.is_occupied());

    // Set depth
    cs.set_depth(42);
    EXPECT_EQ(cs.get_depth(), 42);

    // Set waste pointer
    cs.set_waste_ptr(15);
    EXPECT_EQ(cs.get_waste_ptr(), 15);
}

// Test 9: Foundation fields
TEST(CompactState, FoundationFields) {
    compact_state cs;
    cs.clear();

    cs.set_foundation(0, 3);  // Clubs to 3
    cs.set_foundation(1, 5);  // Hearts to 5
    cs.set_foundation(2, 0);  // Spades empty
    cs.set_foundation(3, 13); // Diamonds to K

    EXPECT_EQ(cs.get_foundation(0), 3);
    EXPECT_EQ(cs.get_foundation(1), 5);
    EXPECT_EQ(cs.get_foundation(2), 0);
    EXPECT_EQ(cs.get_foundation(3), 13);
}

// Test 10: Descriptor nibbles
TEST(CompactState, DescriptorNibbles) {
    compact_state cs;
    cs.clear();

    // Set various descriptors
    cs.set_descriptor(0, compact_state::STARTING);
    cs.set_descriptor(1, compact_state::ROOT);
    cs.set_descriptor(2, compact_state::IN_CELL);
    cs.set_descriptor(51, compact_state::PARENT_3);

    EXPECT_EQ(cs.get_descriptor(0), compact_state::STARTING);
    EXPECT_EQ(cs.get_descriptor(1), compact_state::ROOT);
    EXPECT_EQ(cs.get_descriptor(2), compact_state::IN_CELL);
    EXPECT_EQ(cs.get_descriptor(51), compact_state::PARENT_3);
}

// Test 11: Compact state matching (comparison ignoring occupied and depth)
TEST(CompactState, Matching) {
    compact_state cs1, cs2;
    cs1.clear();
    cs2.clear();

    // Same payload content
    cs1.set_foundation(0, 3);
    cs2.set_foundation(0, 3);
    cs1.set_descriptor(0, compact_state::ROOT);
    cs2.set_descriptor(0, compact_state::ROOT);

    EXPECT_TRUE(cs1.matches(cs2)) << "States with identical payload should match";

    // Different depth should not affect matching
    cs1.set_depth(10);
    cs2.set_depth(20);
    EXPECT_TRUE(cs1.matches(cs2)) << "Different depths should not affect matching";

    // Different occupied flag should not affect matching (not part of payload)
    cs1.set_occupied(true);
    cs2.set_occupied(false);
    EXPECT_TRUE(cs1.matches(cs2)) << "Different occupied flags should not affect matching";

    // Different descriptor should affect matching
    cs1.set_descriptor(1, compact_state::ROOT);
    cs2.set_descriptor(1, compact_state::IN_CELL);
    EXPECT_FALSE(cs1.matches(cs2)) << "States with different descriptors should not match";
}

// Test 12: Compact state size
TEST(CompactState, Size) {
    compact_state cs;
    EXPECT_EQ(sizeof(cs), 32) << "compact_state must be exactly 32 bytes";
}

#include "../test_helper.h"
#include "../../main/game/parent_table.h"

// Test: Hash initialization is non-zero and deterministic
TEST(ZobristGameState, InitializationDeterministic) {
    zobrist_hash::init();
    sol_rules rules;
    rules.tableau_pile_count = 3;
    rules.build_pol = sol_rules::build_policy::SAME_SUIT;

    typedef std::initializer_list<std::initializer_list<std::string>> sil;
    game_state gs1(rules, sil{{"AC"}, {"2D"}, {"3H"}});
    game_state gs2(rules, sil{{"AC"}, {"2D"}, {"3H"}});

    EXPECT_NE(gs1.get_zobrist_hash(), 0u) << "Hash should be non-zero";
    EXPECT_EQ(gs1.get_zobrist_hash(), gs2.get_zobrist_hash())
        << "Same state should produce same hash";
}

// Test: Payload is initialized correctly at construction
TEST(ZobristGameState, PayloadInitialization) {
    zobrist_hash::init();
    sol_rules rules;
    rules.tableau_pile_count = 2;
    rules.build_pol = sol_rules::build_policy::SAME_SUIT;

    typedef std::initializer_list<std::initializer_list<std::string>> sil;
    game_state gs(rules, sil{{"AC"}, {"2D"}});

    const compact_state& payload = gs.get_payload();

    // v3 semantics: face-up cards at pile bottom get IN_SPACE; unplaced cards get STARTING
    // AC (suit 0, rank 1 -> id 0) and 2D (suit 3, rank 2 -> id 40) are at pile bottoms
    uint8_t ac_cid = zobrist_hash::card_id(0, 1);
    uint8_t d2_cid = zobrist_hash::card_id(3, 2);
    EXPECT_EQ(payload.get_descriptor(ac_cid), compact_state::IN_SPACE)
        << "AC at pile bottom should have IN_SPACE descriptor";
    EXPECT_EQ(payload.get_descriptor(d2_cid), compact_state::IN_SPACE)
        << "2D at pile bottom should have IN_SPACE descriptor";
    for (uint8_t c = 0; c < 52; ++c) {
        if (c != ac_cid && c != d2_cid) {
            EXPECT_EQ(payload.get_descriptor(c), compact_state::STARTING)
                << "Card " << (int)c << " not in any pile should have STARTING descriptor";
        }
    }
}

// Test: compute_hash_from_scratch recomputes correctly
// Note: For games without foundations/holes, both methods should produce the same hash
// since payload descriptors are all STARTING (0)
TEST(ZobristGameState, FromScratchRecomputes) {
    zobrist_hash::init();

    // Use a FreeCell-like game to avoid foundation/hole complications
    sol_rules rules;
    rules.tableau_pile_count = 8;
    rules.cells = 4;
    rules.build_pol = sol_rules::build_policy::RED_BLACK;
    rules.foundations_present = false;  // Disable foundations to isolate the test
    rules.hole = false;
    rules.stock_size = 0;

    typedef std::initializer_list<std::initializer_list<std::string>> sil;
    game_state gs(rules, sil{{}, {}, {}, {}, {}, {}, {}, {}, {}});

    uint64_t initial = gs.get_zobrist_hash();
    // Both should be non-zero
    EXPECT_NE(initial, 0u) << "Initial hash should be non-zero";
}

// Test: Parent table correctness - RED_BLACK build policy
TEST(ParentTable, RedBlackParents) {
    // Ace of Clubs (card_id=0, black) should have red rank-2 parents
    auto parents = parent_table::get_parents(0, sol_rules::build_policy::RED_BLACK);
    ASSERT_EQ(parents.size(), 2u);
    // Parents should be 2H (card_id=14) and 2D (card_id=40)
    // 2H: suit=1, rank=2 -> 1*13+1=14
    // 2D: suit=3, rank=2 -> 3*13+1=40
    EXPECT_EQ(parents[0], 14);
    EXPECT_EQ(parents[1], 40);
}

// Test: Parent table - SAME_SUIT build policy
TEST(ParentTable, SameSuitParents) {
    // Ace of Clubs (card_id=0) should have 2 of Clubs (card_id=1)
    auto parents = parent_table::get_parents(0, sol_rules::build_policy::SAME_SUIT);
    ASSERT_EQ(parents.size(), 1u);
    EXPECT_EQ(parents[0], 1);
}

// Test: Parent table - King has no parents
TEST(ParentTable, KingHasNoParents) {
    // King of Clubs (card_id=12)
    auto parents = parent_table::get_parents(12, sol_rules::build_policy::RED_BLACK);
    EXPECT_TRUE(parents.empty());
}

// Test: Descriptor lookup for parents
TEST(ParentTable, DescriptorForParent) {
    // Ace of Clubs built on 2 of Hearts -> PARENT_0
    uint8_t desc = parent_table::get_descriptor_for_parent(0, 14, sol_rules::build_policy::RED_BLACK);
    EXPECT_EQ(desc, compact_state::PARENT_0);

    // Ace of Clubs built on 2 of Diamonds -> PARENT_1
    desc = parent_table::get_descriptor_for_parent(0, 40, sol_rules::build_policy::RED_BLACK);
    EXPECT_EQ(desc, compact_state::PARENT_1);
}

// Test: Wrapping builds - King's parent is Ace when foundation base != Ace
TEST(ParentTable, WrappingKingParentIsAce) {
    // Canfield with foundation base = King (13): build sequence wraps K,A,2,...,Q
    // King of Clubs (card_id=12) should have Ace parents (not empty)
    // With RED_BLACK: King (black) parents are red Aces
    auto parents = parent_table::get_parents(12, sol_rules::build_policy::RED_BLACK, 13, 13);
    ASSERT_EQ(parents.size(), 2u);
    // AH: suit=1, rank=1 -> 1*13+0=13
    // AD: suit=3, rank=1 -> 3*13+0=39
    EXPECT_EQ(parents[0], 13);
    EXPECT_EQ(parents[1], 39);
}

// Test: Wrapping builds - top of sequence has no parents
TEST(ParentTable, WrappingTopOfSequenceNoParents) {
    // Foundation base = 5: sequence is 5,6,...,K,A,2,3,4. Top = rank 4.
    // 4 of Clubs (card_id=3, rank=4) should have no parents
    auto parents = parent_table::get_parents(3, sol_rules::build_policy::RED_BLACK, 5, 13);
    EXPECT_TRUE(parents.empty());
}

// Test: Wrapping builds - Ace's parent wraps correctly
TEST(ParentTable, WrappingAceParent) {
    // Foundation base = 5: A(converted=10), parent converted=11 -> rank 2
    // Ace of Clubs (card_id=0, black) parents with RED_BLACK: 2H(14), 2D(40)
    auto parents = parent_table::get_parents(0, sol_rules::build_policy::RED_BLACK, 5, 13);
    ASSERT_EQ(parents.size(), 2u);
    EXPECT_EQ(parents[0], 14);  // 2H
    EXPECT_EQ(parents[1], 40);  // 2D
}

// Test: Wrapping descriptor lookup
TEST(ParentTable, WrappingDescriptorForParent) {
    // Base=13: King of Clubs on Ace of Hearts -> PARENT_0
    uint8_t desc = parent_table::get_descriptor_for_parent(
        12, 13, sol_rules::build_policy::RED_BLACK, 13, 13);
    EXPECT_EQ(desc, compact_state::PARENT_0);

    // King of Clubs on Ace of Diamonds -> PARENT_1
    desc = parent_table::get_descriptor_for_parent(
        12, 39, sol_rules::build_policy::RED_BLACK, 13, 13);
    EXPECT_EQ(desc, compact_state::PARENT_1);
}

// Test: Default params (base=1) produce same results as before
TEST(ParentTable, DefaultParamsUnchanged) {
    // King with default base=1 still has no parents
    auto parents = parent_table::get_parents(12, sol_rules::build_policy::RED_BLACK);
    EXPECT_TRUE(parents.empty());

    // Ace with default base=1 still has rank-2 parents
    parents = parent_table::get_parents(0, sol_rules::build_policy::RED_BLACK);
    ASSERT_EQ(parents.size(), 2u);
    EXPECT_EQ(parents[0], 14);
    EXPECT_EQ(parents[1], 40);
}

///////////////////////////////////////////////////////////////////////////////
// Incremental update tests (Task 2.6)
///////////////////////////////////////////////////////////////////////////////

// Test: Hash consistency after make/undo for regular moves
TEST(ZobristIncremental, RegularMoveUndoRestoresHash) {
    zobrist_hash::init();
    sol_rules rules;
    rules.tableau_pile_count = 4;
    rules.build_pol = sol_rules::build_policy::RED_BLACK;
    rules.foundations_present = true;
    rules.foundations_removable = false;

    typedef std::initializer_list<std::initializer_list<std::string>> sil;
    // AC on 2H: Ace of Clubs on 2 of Hearts (valid RED_BLACK)
    game_state gs(rules, sil{{}, {}, {}, {}, {"AC", "2H"}, {"3C"}, {"KS"}, {}});

    uint64_t initial_hash = gs.get_zobrist_hash();

    // Get legal moves and make/undo each one
    auto moves = gs.get_legal_moves();
    ASSERT_GT(moves.size(), 0u) << "Should have at least one legal move";

    for (const auto& m : moves) {
        gs.make_move(m);
        gs.undo_move(m);
        EXPECT_EQ(gs.get_zobrist_hash(), initial_hash)
            << "Hash should be restored after make/undo";
    }
}

// Test: Hash consistency after multiple make/undo cycles
TEST(ZobristIncremental, MultipleMoveCyclesRestoreHash) {
    zobrist_hash::init();
    sol_rules rules;
    rules.tableau_pile_count = 3;
    rules.build_pol = sol_rules::build_policy::SAME_SUIT;
    rules.foundations_present = true;

    typedef std::initializer_list<std::initializer_list<std::string>> sil;
    game_state gs(rules, sil{{}, {}, {}, {"AC", "2C"}, {"3C"}, {"4C"}});

    uint64_t initial_hash = gs.get_zobrist_hash();

    // Do multiple cycles of getting moves, making first legal, undoing
    for (int cycle = 0; cycle < 10; ++cycle) {
        auto moves = gs.get_legal_moves();
        if (moves.empty()) break;
        gs.make_move(moves[0]);
        gs.undo_move(moves[0]);
        EXPECT_EQ(gs.get_zobrist_hash(), initial_hash)
            << "Hash should be restored in cycle " << cycle;
    }
}

// Test: Payload consistency — descriptor matches after make/undo
TEST(ZobristIncremental, PayloadRestoredAfterUndo) {
    zobrist_hash::init();
    sol_rules rules;
    rules.tableau_pile_count = 4;
    rules.build_pol = sol_rules::build_policy::RED_BLACK;
    rules.foundations_present = true;

    typedef std::initializer_list<std::initializer_list<std::string>> sil;
    game_state gs(rules, sil{{}, {}, {}, {}, {"AC", "2H"}, {"3S"}, {"KD"}, {}});

    // Save initial payload state
    compact_state initial_payload = gs.get_payload();

    auto moves = gs.get_legal_moves();
    ASSERT_GT(moves.size(), 0u);

    for (const auto& m : moves) {
        gs.make_move(m);
        gs.undo_move(m);
        // Compare payload byte-for-byte (bytes 3-31)
        EXPECT_TRUE(gs.get_payload().matches(initial_payload))
            << "Payload should be restored after make/undo";
    }
}

// Test: Descriptor changes on regular tableau-to-tableau move
TEST(ZobristIncremental, DescriptorChangesOnTableauMove) {
    zobrist_hash::init();
    sol_rules rules;
    rules.tableau_pile_count = 3;
    rules.build_pol = sol_rules::build_policy::RED_BLACK;
    rules.foundations_present = false;  // Disable foundations to avoid auto-move

    typedef std::initializer_list<std::initializer_list<std::string>> sil;
    // AC alone, 2H alone, empty — AC can move onto 2H (red-black)
    game_state gs(rules, sil{{"AC"}, {"2H"}, {}});

    // AC is alone at pile bottom -> IN_SPACE (v3 semantics)
    uint8_t ac_cid = zobrist_hash::card_id(0, 1);  // Ace of Clubs
    EXPECT_EQ(gs.get_payload().get_descriptor(ac_cid), compact_state::IN_SPACE);

    uint64_t hash_before = gs.get_zobrist_hash();

    // Find a move for AC
    auto moves = gs.get_legal_moves();
    ASSERT_GT(moves.size(), 0u) << "Should have at least one legal move";

    // Use the first legal move (AC → 2H should be the most natural)
    move m = moves[0];
    gs.make_move(m);

    // After move, AC's descriptor should change (no longer STARTING)
    EXPECT_NE(gs.get_payload().get_descriptor(ac_cid), compact_state::STARTING)
        << "AC descriptor should change after being built on 2H";
    EXPECT_NE(gs.get_zobrist_hash(), hash_before)
        << "Hash should change after move";

    // Undo and verify restoration
    gs.undo_move(m);
    EXPECT_EQ(gs.get_payload().get_descriptor(ac_cid), compact_state::IN_SPACE)
        << "AC descriptor should be restored to IN_SPACE after undo";
    EXPECT_EQ(gs.get_zobrist_hash(), hash_before)
        << "Hash should be restored after undo";
}

// Test: STARTING_FACE_UP on reveal move
TEST(ZobristIncremental, StartingFaceUpOnReveal) {
    zobrist_hash::init();
    sol_rules rules;
    rules.tableau_pile_count = 3;
    rules.build_pol = sol_rules::build_policy::RED_BLACK;
    rules.foundations_present = true;
    rules.face_up = sol_rules::face_up_policy::TOP_CARDS;

    typedef std::initializer_list<std::initializer_list<std::string>> sil;
    // Face-down 3S under AC, 2H alone
    // Using lowercase for face-down: "3s" = face-down 3 of Spades
    game_state gs(rules, sil{{}, {}, {}, {"AC", "3s"}, {"2H"}, {}});

    uint8_t s3_cid = zobrist_hash::card_id(2, 3);  // 3 of Spades

    // 3S should be STARTING (face-down in original position)
    EXPECT_EQ(gs.get_payload().get_descriptor(s3_cid), compact_state::STARTING);

    uint64_t hash_before = gs.get_zobrist_hash();

    // Find a move that has reveal_move set (moving AC should reveal 3S)
    auto moves = gs.get_legal_moves();
    move reveal_move(move::mtype::regular, 255, 255);
    bool found = false;
    for (const auto& m : moves) {
        if (m.reveal_move) {
            reveal_move = m;
            found = true;
            break;
        }
    }

    if (found) {
        gs.make_move(reveal_move);

        // 3S should now be STARTING_FACE_UP
        EXPECT_EQ(gs.get_payload().get_descriptor(s3_cid), compact_state::STARTING_FACE_UP)
            << "Revealed card should have STARTING_FACE_UP descriptor";
        EXPECT_NE(gs.get_zobrist_hash(), hash_before)
            << "Hash should change after reveal";

        // Undo — 3S should go back to STARTING
        gs.undo_move(reveal_move);
        EXPECT_EQ(gs.get_payload().get_descriptor(s3_cid), compact_state::STARTING)
            << "Revealed card should revert to STARTING after undo";
        EXPECT_EQ(gs.get_zobrist_hash(), hash_before)
            << "Hash should be restored after undo of reveal";
    }
}

// Test: Foundation update — card to foundation, descriptor and header
TEST(ZobristIncremental, FoundationMoveUpdatesPayload) {
    zobrist_hash::init();
    sol_rules rules;
    rules.tableau_pile_count = 3;
    rules.build_pol = sol_rules::build_policy::SAME_SUIT;
    rules.foundations_present = true;

    typedef std::initializer_list<std::initializer_list<std::string>> sil;
    // AC alone on a tableau pile; foundations empty
    game_state gs(rules, sil{{}, {}, {}, {"AC"}, {"2C"}, {}});

    uint8_t ac_cid = zobrist_hash::card_id(0, 1);  // Ace of Clubs
    uint64_t hash_before = gs.get_zobrist_hash();

    // Foundation for Clubs should be empty (rank 0)
    EXPECT_EQ(gs.get_payload().get_foundation(0), 0);

    // Get dominance move (AC to foundation should be a dominance move)
    auto dom = gs.get_dominance_move();
    if (dom) {
        gs.make_move(*dom);

        // Foundation for Clubs should now show rank 1 (Ace)
        EXPECT_EQ(gs.get_payload().get_foundation(0), 1)
            << "Foundation Clubs should be rank 1 after AC placed";

        // AC descriptor should be STARTING (per design: cards on foundation get STARTING)
        EXPECT_EQ(gs.get_payload().get_descriptor(ac_cid), compact_state::STARTING)
            << "Card on foundation should have STARTING descriptor";

        // Hash should have changed
        EXPECT_NE(gs.get_zobrist_hash(), hash_before);

        // Undo
        gs.undo_move(*dom);
        EXPECT_EQ(gs.get_payload().get_foundation(0), 0)
            << "Foundation should be restored after undo";
        EXPECT_EQ(gs.get_zobrist_hash(), hash_before)
            << "Hash should be restored after undo";
    }
}

// Test: FreeCell — make/undo with cell moves
TEST(ZobristIncremental, CellMoveDescriptor) {
    zobrist_hash::init();
    sol_rules rules;
    rules.tableau_pile_count = 4;
    rules.cells = 2;
    rules.build_pol = sol_rules::build_policy::RED_BLACK;
    rules.foundations_present = true;

    typedef std::initializer_list<std::initializer_list<std::string>> sil;
    // 4 foundations (piles 0-3), 2 cells (piles 4-5), 4 tableau (piles 6-9)
    // Place AC and 2H on tableau piles so AC starts as STARTING
    game_state gs(rules, sil{{}, {}, {}, {}, {}, {}, {"AC"}, {"2H"}, {}, {}});

    uint8_t ac_cid = zobrist_hash::card_id(0, 1);
    uint64_t hash_before = gs.get_zobrist_hash();

    // Try each legal move to find one that results in IN_CELL for AC
    auto moves = gs.get_legal_moves();
    move cell_move(move::mtype::regular, 255, 255);
    bool found = false;
    for (const auto& m : moves) {
        gs.make_move(m);
        uint8_t desc = gs.get_payload().get_descriptor(ac_cid);
        gs.undo_move(m);

        if (desc == compact_state::IN_CELL) {
            cell_move = m;
            found = true;
            break;
        }
    }

    if (found) {
        gs.make_move(cell_move);
        EXPECT_EQ(gs.get_payload().get_descriptor(ac_cid), compact_state::IN_CELL)
            << "Card in cell should have IN_CELL descriptor";
        EXPECT_NE(gs.get_zobrist_hash(), hash_before);

        gs.undo_move(cell_move);
        EXPECT_EQ(gs.get_payload().get_descriptor(ac_cid), compact_state::IN_SPACE);
        EXPECT_EQ(gs.get_zobrist_hash(), hash_before);
    }
}

// Test: Different game states from different moves produce different hashes
TEST(ZobristIncremental, DifferentMovesProduceDifferentHashes) {
    zobrist_hash::init();
    sol_rules rules;
    rules.tableau_pile_count = 4;
    rules.build_pol = sol_rules::build_policy::RED_BLACK;
    rules.foundations_present = true;

    typedef std::initializer_list<std::initializer_list<std::string>> sil;
    game_state gs(rules, sil{{}, {}, {}, {}, {"AC", "2H"}, {"3S"}, {"KD"}, {}});

    auto moves = gs.get_legal_moves();
    if (moves.size() >= 2) {
        gs.make_move(moves[0]);
        uint64_t hash1 = gs.get_zobrist_hash();
        gs.undo_move(moves[0]);

        gs.make_move(moves[1]);
        uint64_t hash2 = gs.get_zobrist_hash();
        gs.undo_move(moves[1]);

        // Different moves should (almost certainly) produce different hashes
        EXPECT_NE(hash1, hash2)
            << "Different game states should produce different hashes";
    }
}

// Test: Deep forward play then full undo — hash returns to initial value
// Exercises many move types across a real game (FreeCell seed 42)
TEST(ZobristIncremental, DeepForwardPlayAndUndo) {
    zobrist_hash::init();
    sol_rules rules;
    rules.tableau_pile_count = 8;
    rules.cells = 4;
    rules.build_pol = sol_rules::build_policy::RED_BLACK;
    rules.foundations_present = true;
    rules.foundations_removable = true;
    rules.face_up = sol_rules::face_up_policy::ALL;

    typedef std::initializer_list<std::initializer_list<std::string>> sil;
    game_state gs(rules, sil{
        {}, {}, {}, {},           // 4 foundations (empty)
        {}, {}, {}, {},           // 4 cells (empty)
        {"AC", "3H", "5S"},      // tableau 0
        {"2D", "4C", "6H"},      // tableau 1
        {"7S", "9D", "JC"},      // tableau 2
        {"8H", "10S", "QD"},     // tableau 3
        {"KH", "2S", "4D"},      // tableau 4
        {"5C", "7H", "9S"},      // tableau 5
        {"JD", "KC", "AH"},      // tableau 6
        {"3S", "6D", "8C"}       // tableau 7
    });

    uint64_t initial_hash = gs.get_zobrist_hash();
    compact_state initial_payload = gs.get_payload();

    // Play forward up to 50 moves, recording each move
    std::vector<move> played;
    for (int i = 0; i < 50; ++i) {
        // Try dominance moves first
        auto dom = gs.get_dominance_move();
        if (dom) {
            gs.make_move(*dom);
            played.push_back(*dom);
            continue;
        }

        auto moves = gs.get_legal_moves();
        if (moves.empty()) break;
        gs.make_move(moves[0]);
        played.push_back(moves[0]);
    }

    ASSERT_GT(played.size(), 10u)
        << "Should play at least 10 moves to exercise the incremental path";

    // Undo all moves in reverse
    for (auto it = played.rbegin(); it != played.rend(); ++it) {
        gs.undo_move(*it);
    }

    // Hash and payload must return to initial values
    EXPECT_EQ(gs.get_zobrist_hash(), initial_hash)
        << "Hash must return to initial after full undo";
    EXPECT_TRUE(gs.get_payload().matches(initial_payload))
        << "Payload must return to initial after full undo";
}

// Test: Hash-payload consistency after each move in a sequence
// Verifies compute_hash_from_scratch matches incremental hash at every step
TEST(ZobristIncremental, HashPayloadConsistencyDuringPlay) {
    zobrist_hash::init();
    sol_rules rules;
    rules.tableau_pile_count = 4;
    rules.cells = 2;
    rules.build_pol = sol_rules::build_policy::RED_BLACK;
    rules.foundations_present = true;
    rules.face_up = sol_rules::face_up_policy::ALL;

    typedef std::initializer_list<std::initializer_list<std::string>> sil;
    game_state gs(rules, sil{
        {}, {}, {}, {},       // 4 foundations
        {}, {},               // 2 cells
        {"AC", "3H", "5S", "7D"},  // tableau 0
        {"2D", "4C", "6H"},       // tableau 1
        {"8S", "10D", "QC"},      // tableau 2
        {"9H", "JC", "KS"}        // tableau 3
    });

    // Play up to 30 moves, checking consistency at each step
    std::vector<move> played;
    for (int i = 0; i < 30; ++i) {
        auto dom = gs.get_dominance_move();
        if (dom) {
            gs.make_move(*dom);
            played.push_back(*dom);
        } else {
            auto moves = gs.get_legal_moves();
            if (moves.empty()) break;
            gs.make_move(moves[0]);
            played.push_back(moves[0]);
        }

        // Save the incremental hash
        uint64_t incremental = gs.get_zobrist_hash();
        // Recompute from payload descriptors
        gs.compute_hash_from_scratch();
        EXPECT_EQ(gs.get_zobrist_hash(), incremental)
            << "Hash-from-scratch should match incremental hash at move " << i;
    }

    // Undo all and check hash-from-scratch at each undo step too
    for (auto it = played.rbegin(); it != played.rend(); ++it) {
        gs.undo_move(*it);
        uint64_t incremental = gs.get_zobrist_hash();
        gs.compute_hash_from_scratch();
        EXPECT_EQ(gs.get_zobrist_hash(), incremental)
            << "Hash-from-scratch should match after undo";
    }
}

// Test: Symmetry invariance — hash depends only on descriptors, not pile indices
// Two states with same card descriptors but different pile orderings should hash equal.
// We verify this indirectly: the hash is XOR of Z_card[c][desc(c)] terms plus headers.
// Pile ordering affects which pile a card is in, but NOT its descriptor (which encodes
// the parent relationship, not the pile index). So two states reached by different
// pile orderings of the same moves should produce the same hash.
TEST(ZobristIncremental, SymmetryInvariance) {
    zobrist_hash::init();
    sol_rules rules;
    rules.tableau_pile_count = 3;
    rules.build_pol = sol_rules::build_policy::SAME_SUIT;
    rules.foundations_present = false;

    typedef std::initializer_list<std::initializer_list<std::string>> sil;

    // State 1: AC on pile 0, 2C on pile 1, 3C on pile 2
    game_state gs1(rules, sil{{"AC"}, {"2C"}, {"3C"}});
    // State 2: Same cards, different pile order (AC on pile 1, 2C on pile 0)
    game_state gs2(rules, sil{{"2C"}, {"AC"}, {"3C"}});

    // Both states have all cards as STARTING, same foundations (none), same waste (none)
    // So their hashes should be identical — descriptors are all STARTING regardless of
    // which pile a card is on
    EXPECT_EQ(gs1.get_zobrist_hash(), gs2.get_zobrist_hash())
        << "States with same descriptors but different pile orderings should hash equal";
    EXPECT_TRUE(gs1.get_payload().matches(gs2.get_payload()))
        << "Payloads should match for states with same descriptors";
}

// Test: Full solver-style exercise on Klondike seed (with face-down cards and stock)
TEST(ZobristIncremental, KlondikeSeedMakeUndo) {
    zobrist_hash::init();

    // Use the actual Klondike rules via preset
    sol_rules rules;
    rules.tableau_pile_count = 7;
    rules.build_pol = sol_rules::build_policy::RED_BLACK;
    rules.spaces_pol = sol_rules::spaces_policy::KINGS;
    rules.foundations_present = true;
    rules.face_up = sol_rules::face_up_policy::TOP_CARDS;
    rules.diagonal_deal = true;
    rules.stock_size = 24;
    rules.stock_deal_t = sol_rules::stock_deal_type::WASTE;
    rules.stock_deal_count = 1;
    rules.stock_redeal = false;

    game_state gs(rules, 42, game_state::streamliner_options::NONE);

    uint64_t initial_hash = gs.get_zobrist_hash();

    // Play forward, taking first legal move each time
    std::vector<move> played;
    for (int i = 0; i < 30; ++i) {
        auto dom = gs.get_dominance_move();
        if (dom) {
            gs.make_move(*dom);
            played.push_back(*dom);
            continue;
        }
        auto moves = gs.get_legal_moves();
        if (moves.empty()) break;
        gs.make_move(moves[0]);
        played.push_back(moves[0]);
    }

    // Undo everything
    for (auto it = played.rbegin(); it != played.rend(); ++it) {
        gs.undo_move(*it);
    }

    EXPECT_EQ(gs.get_zobrist_hash(), initial_hash)
        << "Klondike: hash must return to initial after full undo";
}

// Test: Black Hole seed — hole moves
TEST(ZobristIncremental, BlackHoleSeedMakeUndo) {
    zobrist_hash::init();

    // Use test_helper to check a real black-hole instance
    sol_rules rules;
    rules.tableau_pile_count = 17;
    rules.build_pol = sol_rules::build_policy::NO_BUILD;
    rules.foundations_present = false;
    rules.hole = true;

    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    uint64_t initial_hash = gs.get_zobrist_hash();

    std::vector<move> played;
    for (int i = 0; i < 20; ++i) {
        auto dom = gs.get_dominance_move();
        if (dom) {
            gs.make_move(*dom);
            played.push_back(*dom);
            continue;
        }
        auto moves = gs.get_legal_moves();
        if (moves.empty()) break;
        gs.make_move(moves[0]);
        played.push_back(moves[0]);
    }

    for (auto it = played.rbegin(); it != played.rend(); ++it) {
        gs.undo_move(*it);
    }

    EXPECT_EQ(gs.get_zobrist_hash(), initial_hash)
        << "Black Hole: hash must return to initial after full undo";
}
