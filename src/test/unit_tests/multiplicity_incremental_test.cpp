// ─── multiplicity_incremental_test.cpp ──────────────────────────────────────
//
// Stage 4+5 unit tests: validate incremental updates in the multiplicity
// descriptor engine for NONE mode (Stage 4) and COLOUR/SUIT_IRRELEVANT
// modes with BFS cascade (Stage 5).
//
// Tests verify that incremental_update_none() and incremental_update()
// produce identical hash and payload to a from-scratch
// recompute_from_descriptors() for the same descriptor state.
// ─────────────────────────────────────────────────────────────────────────────

#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>
#include <random>

#include "../../main/game/multiplicity_descriptor_engine.h"
#include "../../main/game/multiplicity_zobrist.h"
#include "../../main/game/multiplicity_descriptor.h"
#include "../../main/game/multiplicity_static_class.h"
#include "../../main/game/sol_rules.h"

// ═════════════════════════════════════════════════════════════════════════════
// Helpers
// ═════════════════════════════════════════════════════════════════════════════

// Set up an engine in the given symmetry mode with the given descriptors.
static void setup_engine(multiplicity_descriptor_engine& eng,
                         const multiplicity_descriptor descriptors[52],
                         symmetry_mode mode = symmetry_mode::NONE) {
    eng.classes.init(mode);
    for (uint8_t c = 0; c < 52; c++)
        eng.descriptors[c] = descriptors[c];
    // Use recompute_hash to rebuild everything from descriptors
    sol_rules dummy;
    eng.recompute_hash(dummy);
}

// Verify that the engine's current hash and payload match a fresh from-scratch
// computation on the same descriptors.  Saves and restores ALL engine state
// so the incremental result is preserved for subsequent updates.
static void verify_matches_scratch(multiplicity_descriptor_engine& eng) {
    // Save incremental state
    uint64_t saved_hash = eng.hash_value;
    multiplicity_descriptor_store saved_store = eng.store;
    uint8_t saved_slot[104], saved_canonical[104];
    int8_t saved_children[104];
    uint64_t saved_class_sum[52];
    uint8_t saved_class_members[52];
    std::memcpy(saved_slot, eng.slot, 52);
    std::memcpy(saved_canonical, eng.canonical_pos, 52);
    std::memcpy(saved_children, eng.children, 52);
    std::memcpy(saved_class_sum, eng.class_sum, sizeof(saved_class_sum));
    std::memcpy(saved_class_members, eng.classes.class_members, 52);

    // Recompute from scratch
    sol_rules dummy;
    eng.recompute_hash(dummy);

    // Compare
    EXPECT_EQ(eng.hash_value, saved_hash) << "hash mismatch";
    EXPECT_TRUE(eng.store.matches(saved_store)) << "payload mismatch";

    // Restore ALL incremental state
    eng.hash_value = saved_hash;
    eng.store = saved_store;
    std::memcpy(eng.slot, saved_slot, 52);
    std::memcpy(eng.canonical_pos, saved_canonical, 52);
    std::memcpy(eng.children, saved_children, 52);
    std::memcpy(eng.class_sum, saved_class_sum, sizeof(saved_class_sum));
    std::memcpy(eng.classes.class_members, saved_class_members, 52);
}

// ═════════════════════════════════════════════════════════════════════════════
// Tests
// ═════════════════════════════════════════════════════════════════════════════

// Test: single card changes from locative to predecessor
TEST(MultiplicityIncrementalTest, NoneModeSingleCardMove) {
    multiplicity_descriptor_engine eng;

    // Set up: card 0 in cell, card 1 at pile bottom, card 2 sitting on card 1
    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[0] = multiplicity_descriptor::make_locative(MLD_IN_CELL);
    descs[1] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    descs[2] = multiplicity_descriptor::make_predecessor(1, false);

    setup_engine(eng, descs);

    // Move card 0 from cell to sit on card 1 (card 2 is still on card 1,
    // but for this test we just change card 0's descriptor)
    std::pair<uint8_t, multiplicity_descriptor> changes[1];
    changes[0] = {0, multiplicity_descriptor::make_predecessor(1, false)};
    eng.incremental_update_none(changes, 1);

    verify_matches_scratch(eng);
}

// Test: card changes from predecessor to locative (moved to foundation)
TEST(MultiplicityIncrementalTest, NoneModeToFoundation) {
    multiplicity_descriptor_engine eng;

    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[5] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    descs[10] = multiplicity_descriptor::make_predecessor(5, false);

    setup_engine(eng, descs);

    // Card 10 moves to foundation
    std::pair<uint8_t, multiplicity_descriptor> changes[1];
    changes[0] = {10, multiplicity_descriptor::make_locative(MLD_PERMANENT)};
    eng.incremental_update_none(changes, 1);

    verify_matches_scratch(eng);
}

// Test: two changes (move + reveal)
TEST(MultiplicityIncrementalTest, NoneModeWithReveal) {
    multiplicity_descriptor_engine eng;

    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    // Pile: card 3 (face-up, top), card 4 (face-down), card 5 (bottom)
    descs[5] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    descs[4] = multiplicity_descriptor::make_predecessor(5, true);  // face-down
    descs[3] = multiplicity_descriptor::make_predecessor(4, false);
    // Destination pile: card 20 at bottom
    descs[20] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);

    setup_engine(eng, descs);

    // Move card 3 to sit on card 20; reveal card 4 (face-down → face-up)
    std::pair<uint8_t, multiplicity_descriptor> changes[2];
    changes[0] = {3, multiplicity_descriptor::make_predecessor(20, false)};
    changes[1] = {4, multiplicity_descriptor::make_predecessor(5, false)};  // revealed
    eng.incremental_update_none(changes, 2);

    verify_matches_scratch(eng);
}

// Test: children[] is correctly updated after a move
TEST(MultiplicityIncrementalTest, NoneModeChildrenUpdate) {
    multiplicity_descriptor_engine eng;

    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[10] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    descs[11] = multiplicity_descriptor::make_predecessor(10, false);

    setup_engine(eng, descs);

    // Verify initial children
    EXPECT_EQ(eng.children[10], 11);
    EXPECT_EQ(eng.children[11], -1);

    // Move card 11 to sit on card 20 instead
    descs[20] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    // First set card 20 in the engine
    std::pair<uint8_t, multiplicity_descriptor> setup_changes[1];
    setup_changes[0] = {20, multiplicity_descriptor::make_locative(MLD_IN_SPACE)};
    eng.incremental_update_none(setup_changes, 1);

    std::pair<uint8_t, multiplicity_descriptor> changes[1];
    changes[0] = {11, multiplicity_descriptor::make_predecessor(20, false)};
    eng.incremental_update_none(changes, 1);

    // children[10] should be -1 now (no card sits on 10)
    EXPECT_EQ(eng.children[10], -1);
    // children[20] should be 11
    EXPECT_EQ(eng.children[20], 11);

    verify_matches_scratch(eng);
}

// Test: class_sum is correctly maintained
TEST(MultiplicityIncrementalTest, NoneModeClassSumUpdate) {
    multiplicity_descriptor_engine eng;

    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[7] = multiplicity_descriptor::make_locative(MLD_IN_CELL);

    setup_engine(eng, descs);

    // Record initial class_sum for card 7
    uint64_t old_sum = eng.class_sum[7];

    // Change card 7 to foundation
    std::pair<uint8_t, multiplicity_descriptor> changes[1];
    changes[0] = {7, multiplicity_descriptor::make_locative(MLD_PERMANENT)};
    eng.incremental_update_none(changes, 1);

    // class_sum[7] should have changed
    EXPECT_NE(eng.class_sum[7], old_sum);

    verify_matches_scratch(eng);
}

// Test: locative-to-locative change (cell to foundation)
TEST(MultiplicityIncrementalTest, NoneModeLocativeChange) {
    multiplicity_descriptor_engine eng;

    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[15] = multiplicity_descriptor::make_locative(MLD_IN_CELL);

    setup_engine(eng, descs);

    std::pair<uint8_t, multiplicity_descriptor> changes[1];
    changes[0] = {15, multiplicity_descriptor::make_locative(MLD_PERMANENT)};
    eng.incremental_update_none(changes, 1);

    verify_matches_scratch(eng);
}

// Test: predecessor-to-predecessor change (card moves between tableau piles)
TEST(MultiplicityIncrementalTest, NoneModePredecessorChange) {
    multiplicity_descriptor_engine eng;

    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[30] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    descs[31] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    descs[10] = multiplicity_descriptor::make_predecessor(30, false);

    setup_engine(eng, descs);

    // Move card 10 from sitting on 30 to sitting on 31
    std::pair<uint8_t, multiplicity_descriptor> changes[1];
    changes[0] = {10, multiplicity_descriptor::make_predecessor(31, false)};
    eng.incremental_update_none(changes, 1);

    verify_matches_scratch(eng);
}

// Test: sequence of moves and undos
TEST(MultiplicityIncrementalTest, NoneModeSequenceOfMoves) {
    multiplicity_descriptor_engine eng;

    // Set up a realistic-ish initial state
    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);

    // 4 tableau piles
    // Pile 0: card 0 (top), card 1, card 2 (bottom)
    descs[2] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    descs[1] = multiplicity_descriptor::make_predecessor(2, true);
    descs[0] = multiplicity_descriptor::make_predecessor(1, false);
    // Pile 1: card 10 (top), card 11 (bottom)
    descs[11] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    descs[10] = multiplicity_descriptor::make_predecessor(11, false);
    // Pile 2: card 20 (alone)
    descs[20] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    // Pile 3: card 30 (alone)
    descs[30] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);

    // Some cards in cells
    descs[40] = multiplicity_descriptor::make_locative(MLD_IN_CELL);
    descs[41] = multiplicity_descriptor::make_locative(MLD_IN_CELL);

    setup_engine(eng, descs);

    // Move 1: card 0 from pile 0 to pile 2 (on card 20)
    {
        std::pair<uint8_t, multiplicity_descriptor> ch[2];
        ch[0] = {0, multiplicity_descriptor::make_predecessor(20, false)};
        ch[1] = {1, multiplicity_descriptor::make_predecessor(2, false)};  // reveal
        eng.incremental_update_none(ch, 2);
        verify_matches_scratch(eng);
    }

    // Move 2: card 40 from cell to pile 3 (on card 30)
    {
        std::pair<uint8_t, multiplicity_descriptor> ch[1];
        ch[0] = {40, multiplicity_descriptor::make_predecessor(30, false)};
        eng.incremental_update_none(ch, 1);
        verify_matches_scratch(eng);
    }

    // Undo move 2: card 40 back to cell
    {
        std::pair<uint8_t, multiplicity_descriptor> ch[1];
        ch[0] = {40, multiplicity_descriptor::make_locative(MLD_IN_CELL)};
        eng.incremental_update_none(ch, 1);
        verify_matches_scratch(eng);
    }

    // Undo move 1: card 0 back to pile 0, card 1 face-down again
    {
        std::pair<uint8_t, multiplicity_descriptor> ch[2];
        ch[0] = {0, multiplicity_descriptor::make_predecessor(1, false)};
        ch[1] = {1, multiplicity_descriptor::make_predecessor(2, true)};  // un-reveal
        eng.incremental_update_none(ch, 2);
        verify_matches_scratch(eng);
    }

    // Move 3: card 10 to foundation
    {
        std::pair<uint8_t, multiplicity_descriptor> ch[1];
        ch[0] = {10, multiplicity_descriptor::make_locative(MLD_PERMANENT)};
        eng.incremental_update_none(ch, 1);
        verify_matches_scratch(eng);
    }
}

// Test: full cascade method works correctly in NONE mode (no cascade fires)
TEST(MultiplicityIncrementalTest, NoneModeFullIncrementalMethod) {
    multiplicity_descriptor_engine eng;

    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[5] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    descs[10] = multiplicity_descriptor::make_predecessor(5, false);

    setup_engine(eng, descs);

    // Use the full incremental_update (not _none) in NONE mode
    std::pair<uint8_t, multiplicity_descriptor> changes[1];
    changes[0] = {10, multiplicity_descriptor::make_locative(MLD_IN_CELL)};
    eng.incremental_update(changes, 1);

    verify_matches_scratch(eng);
}

// Test: face-down flag change (predecessor face_down toggle)
TEST(MultiplicityIncrementalTest, NoneModeFaceDownToggle) {
    multiplicity_descriptor_engine eng;

    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[25] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    descs[26] = multiplicity_descriptor::make_predecessor(25, true);  // face-down

    setup_engine(eng, descs);

    // Reveal card 26 (face-down → face-up, same predecessor)
    std::pair<uint8_t, multiplicity_descriptor> changes[1];
    changes[0] = {26, multiplicity_descriptor::make_predecessor(25, false)};
    eng.incremental_update_none(changes, 1);

    verify_matches_scratch(eng);
}

// ═════════════════════════════════════════════════════════════════════════════
// Stage 5 — Cascade tests (COLOUR and SUIT_IRRELEVANT modes)
// ═════════════════════════════════════════════════════════════════════════════
//
// Card ID layout (Solvitaire encoding):
//   Clubs(black):    cid  0-12  (suit 0)
//   Hearts(red):     cid 13-25  (suit 1)
//   Spades(black):   cid 26-38  (suit 2)
//   Diamonds(red):   cid 39-51  (suit 3)
//
// COLOUR mode (26 classes of 2):
//   class_id = rank_idx*2 + colour  (colour: 0=black, 1=red)
//   Class 0: AC(0), AS(26)   — black Aces
//   Class 1: AH(13), AD(39)  — red Aces
//   Class 2: 2C(1), 2S(27)   — black 2s
//   etc.
//
// SUIT_IRRELEVANT mode (13 classes of 4):
//   class_id = rank_idx = cid % 13
//   Class 0: AC(0), AH(13), AS(26), AD(39)  — all Aces
//   Class 1: 2C(1), 2H(14), 2S(27), 2D(40)  — all 2s
//   etc.

// Test: sort reorder within a class triggers cascade to children
TEST(MultiplicityIncrementalTest, CascadeSortReorder) {
    multiplicity_descriptor_engine eng;

    // COLOUR mode: AC(0) and AS(26) are in class 0.
    // Set up: AC at IN_SPACE, AS at IN_CELL.
    // A third card (2H, cid 14) sits on AC.
    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[0]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE);  // AC
    descs[26] = multiplicity_descriptor::make_locative(MLD_IN_CELL);   // AS
    descs[14] = multiplicity_descriptor::make_predecessor(0, false);   // 2H sits on AC

    setup_engine(eng, descs, symmetry_mode::COLOUR);

    // Change AS from IN_CELL to IN_SPACE.
    // Both class members now have the same locative kind, which may reorder
    // the class sort (since slot bytes change).  The cascade should propagate
    // to 2H (child of AC) if AC's canonical_pos changes.
    std::pair<uint8_t, multiplicity_descriptor> changes[1];
    changes[0] = {26, multiplicity_descriptor::make_locative(MLD_IN_SPACE)};
    eng.incremental_update(changes, 1);

    verify_matches_scratch(eng);
}

// Test: two class members with different descriptors become identical (merge)
TEST(MultiplicityIncrementalTest, CascadeDynamicClassMerge) {
    multiplicity_descriptor_engine eng;

    // COLOUR mode: AC(0) and AS(26) in class 0.
    // AC at IN_SPACE, AS at IN_CELL (different slot bytes).
    // 2H(14) sits on AC, 3H(15) sits on AS.
    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[0]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE);  // AC
    descs[26] = multiplicity_descriptor::make_locative(MLD_IN_CELL);   // AS
    descs[14] = multiplicity_descriptor::make_predecessor(0, false);   // 2H on AC
    descs[15] = multiplicity_descriptor::make_predecessor(26, false);  // 3H on AS

    setup_engine(eng, descs, symmetry_mode::COLOUR);

    // Now change AS from IN_CELL to IN_SPACE — same as AC.
    // After this, AC and AS have identical slot bytes (both IN_SPACE).
    // They become indistinguishable (dynamic class merge).
    // The cascade should update 2H and 3H to use collapsed_pos.
    std::pair<uint8_t, multiplicity_descriptor> changes[1];
    changes[0] = {26, multiplicity_descriptor::make_locative(MLD_IN_SPACE)};
    eng.incremental_update(changes, 1);

    verify_matches_scratch(eng);
}

// Test: two class members with identical descriptors diverge (split)
TEST(MultiplicityIncrementalTest, CascadeDynamicClassSplit) {
    multiplicity_descriptor_engine eng;

    // COLOUR mode: AC(0) and AS(26) in class 0.
    // Both at IN_SPACE (same descriptor, same dynamic class).
    // 2H(14) sits on AC, 3H(15) sits on AS.
    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[0]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE);  // AC
    descs[26] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);  // AS
    descs[14] = multiplicity_descriptor::make_predecessor(0, false);   // 2H on AC
    descs[15] = multiplicity_descriptor::make_predecessor(26, false);  // 3H on AS

    setup_engine(eng, descs, symmetry_mode::COLOUR);

    // Change AS from IN_SPACE to IN_CELL — now different from AC.
    // This splits the dynamic class.  Children 2H and 3H should see
    // divergent canonical positions for their predecessors.
    std::pair<uint8_t, multiplicity_descriptor> changes[1];
    changes[0] = {26, multiplicity_descriptor::make_locative(MLD_IN_CELL)};
    eng.incremental_update(changes, 1);

    verify_matches_scratch(eng);
}

// Test: multi-level cascade (predecessor chain through two classes)
TEST(MultiplicityIncrementalTest, CascadeMultiLevel) {
    multiplicity_descriptor_engine eng;

    // COLOUR mode.
    // Class 0: AC(0), AS(26) — black Aces
    // Class 1: AH(13), AD(39) — red Aces
    //
    // Set up a chain: 2C(1) sits on AH(13), AH sits on AC(0).
    // AC at IN_SPACE, AS at IN_CELL.
    // AD(39) at IN_CELL.
    //
    // Changing AS's descriptor affects class 0's sort order,
    // which changes AC's canonical_pos, which changes AH's slot byte
    // (AH is a predecessor of AC), which changes class 1's sort order,
    // which changes 2C's slot byte — a two-level cascade.
    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[0]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE);  // AC at pile bottom
    descs[26] = multiplicity_descriptor::make_locative(MLD_IN_CELL);   // AS in cell
    descs[13] = multiplicity_descriptor::make_predecessor(0, false);   // AH sits on AC
    descs[39] = multiplicity_descriptor::make_locative(MLD_IN_CELL);   // AD in cell
    descs[1]  = multiplicity_descriptor::make_predecessor(13, false);  // 2C sits on AH

    setup_engine(eng, descs, symmetry_mode::COLOUR);

    // Change AS from IN_CELL to IN_SPACE.
    // This should trigger:
    //   Level 0: class 0 (AC, AS) re-sorts
    //   Level 1: AH (child of AC) recomputes slot → class 1 (AH, AD) re-sorts
    //   Level 2: 2C (child of AH) recomputes slot
    std::pair<uint8_t, multiplicity_descriptor> changes[1];
    changes[0] = {26, multiplicity_descriptor::make_locative(MLD_IN_SPACE)};
    eng.incremental_update(changes, 1);

    verify_matches_scratch(eng);
}

// Test: face-down reveal triggers cascade in COLOUR mode
TEST(MultiplicityIncrementalTest, CascadeRevealFaceDown) {
    multiplicity_descriptor_engine eng;

    // COLOUR mode: 2C(1) and 2S(27) in class 2 (black 2s).
    // 2C is face-down sitting on AC(0).  2S is face-up at IN_SPACE.
    // 3H(15) sits on 2C.
    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[0]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE);  // AC
    descs[1]  = multiplicity_descriptor::make_predecessor(0, true);    // 2C face-down on AC
    descs[27] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);  // 2S face-up
    descs[15] = multiplicity_descriptor::make_predecessor(1, false);   // 3H sits on 2C

    setup_engine(eng, descs, symmetry_mode::COLOUR);

    // Reveal 2C (face-down → face-up).
    // The reflected encoding means 2C's slot byte changes dramatically
    // (255-pos → pos), which should cascade to 3H.
    std::pair<uint8_t, multiplicity_descriptor> changes[1];
    changes[0] = {1, multiplicity_descriptor::make_predecessor(0, false)};
    eng.incremental_update(changes, 1);

    verify_matches_scratch(eng);
}

// Test: SUIT_IRRELEVANT mode with 4-way class interaction
TEST(MultiplicityIncrementalTest, SuitIrrelevantFourWayCascade) {
    multiplicity_descriptor_engine eng;

    // SUIT_IRRELEVANT mode: class 0 has AC(0), AH(13), AS(26), AD(39).
    // Give each a different descriptor:
    //   AC at IN_SPACE, AH at IN_CELL, AS at IN_RESERVE, AD at PERMANENT
    // Put children on AC and AH:
    //   2C(1) sits on AC, 2H(14) sits on AH.
    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[0]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE);    // AC
    descs[13] = multiplicity_descriptor::make_locative(MLD_IN_CELL);     // AH
    descs[26] = multiplicity_descriptor::make_locative(MLD_IN_RESERVE);  // AS
    // descs[39] stays PERMANENT                                          // AD
    descs[1]  = multiplicity_descriptor::make_predecessor(0, false);     // 2C on AC
    descs[14] = multiplicity_descriptor::make_predecessor(13, false);    // 2H on AH

    setup_engine(eng, descs, symmetry_mode::SUIT_IRRELEVANT);

    // Move AS from IN_RESERVE to IN_SPACE (now same as AC).
    // Class 0 re-sorts; AC and AS may swap positions; children 2C/2H update.
    std::pair<uint8_t, multiplicity_descriptor> changes[1];
    changes[0] = {26, multiplicity_descriptor::make_locative(MLD_IN_SPACE)};
    eng.incremental_update(changes, 1);

    verify_matches_scratch(eng);
}

// Test: sequence of moves in COLOUR mode with cascades
TEST(MultiplicityIncrementalTest, ColourModeSequenceOfMoves) {
    multiplicity_descriptor_engine eng;

    // COLOUR mode.  Set up several active cards.
    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);

    // Pile 0: 3H(15) on 2C(1) on AC(0) at bottom
    descs[0]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    descs[1]  = multiplicity_descriptor::make_predecessor(0, false);
    descs[15] = multiplicity_descriptor::make_predecessor(1, false);
    // Pile 1: AS(26) at bottom, 2H(14) on AS
    descs[26] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    descs[14] = multiplicity_descriptor::make_predecessor(26, false);
    // Cell: AH(13)
    descs[13] = multiplicity_descriptor::make_locative(MLD_IN_CELL);

    setup_engine(eng, descs, symmetry_mode::COLOUR);

    // Move 1: 3H(15) from pile 0 to pile 1 (on 2H(14))
    {
        std::pair<uint8_t, multiplicity_descriptor> ch[1];
        ch[0] = {15, multiplicity_descriptor::make_predecessor(14, false)};
        eng.incremental_update(ch, 1);
        verify_matches_scratch(eng);
    }

    // Move 2: AH(13) from cell to pile 0 (on 2C(1))
    {
        std::pair<uint8_t, multiplicity_descriptor> ch[1];
        ch[0] = {13, multiplicity_descriptor::make_predecessor(1, false)};
        eng.incremental_update(ch, 1);
        verify_matches_scratch(eng);
    }

    // Undo move 2: AH(13) back to cell
    {
        std::pair<uint8_t, multiplicity_descriptor> ch[1];
        ch[0] = {13, multiplicity_descriptor::make_locative(MLD_IN_CELL)};
        eng.incremental_update(ch, 1);
        verify_matches_scratch(eng);
    }

    // Move 3: 2C(1) to foundation (PERMANENT)
    {
        std::pair<uint8_t, multiplicity_descriptor> ch[1];
        ch[0] = {1, multiplicity_descriptor::make_locative(MLD_PERMANENT)};
        eng.incremental_update(ch, 1);
        verify_matches_scratch(eng);
    }
}

// Test: COLOUR mode with symmetric permutation produces correct result
TEST(MultiplicityIncrementalTest, ColourSymmetricPermutation) {
    multiplicity_descriptor_engine eng;

    // COLOUR mode: AC(0) and AS(26) in class 0.
    // Set up state A: AC at IN_SPACE, AS at IN_CELL.
    multiplicity_descriptor descs_a[52];
    for (uint8_t c = 0; c < 52; c++)
        descs_a[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs_a[0]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    descs_a[26] = multiplicity_descriptor::make_locative(MLD_IN_CELL);

    setup_engine(eng, descs_a, symmetry_mode::COLOUR);
    uint64_t hash_a = eng.hash_value;
    multiplicity_descriptor_store store_a = eng.store;

    // Set up state B: swap roles — AS at IN_SPACE, AC at IN_CELL.
    // Under COLOUR symmetry, A and B should produce the same hash and payload.
    multiplicity_descriptor descs_b[52];
    for (uint8_t c = 0; c < 52; c++)
        descs_b[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs_b[0]  = multiplicity_descriptor::make_locative(MLD_IN_CELL);
    descs_b[26] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);

    setup_engine(eng, descs_b, symmetry_mode::COLOUR);

    EXPECT_EQ(eng.hash_value, hash_a) << "symmetric permutation should give same hash";
    EXPECT_TRUE(eng.store.matches(store_a)) << "symmetric permutation should give same payload";

    // Now use incremental_update to reach state A from state B.
    // Swap: AC back to IN_SPACE, AS back to IN_CELL.
    std::pair<uint8_t, multiplicity_descriptor> changes[2];
    changes[0] = {0,  multiplicity_descriptor::make_locative(MLD_IN_SPACE)};
    changes[1] = {26, multiplicity_descriptor::make_locative(MLD_IN_CELL)};
    eng.incremental_update(changes, 2);

    // Should still match state A (same canonical form)
    EXPECT_EQ(eng.hash_value, hash_a) << "incremental to symmetric state should match";
    EXPECT_TRUE(eng.store.matches(store_a)) << "incremental to symmetric state should match payload";
    verify_matches_scratch(eng);
}

// ═════════════════════════════════════════════════════════════════════════════
// KI-21: stock_k_plus incremental update tests
//
// Verifies the O(1) waste-top descriptor semantics:
//   - Only waste[0] (top) gets MLD_IN_WASTE; all others get MLD_IN_STOCK.
//   - stock_k_plus incremental changes match recompute_all() from-scratch.
// ═════════════════════════════════════════════════════════════════════════════

// Test: stock deal of 1 card — only the played card's descriptor changes;
// waste top stays the same when count == 1.
TEST(MultiplicityIncrementalTest, StockKPlus_Count1_WasteTopUnchanged) {
    multiplicity_descriptor_engine eng;

    // Set up: stock card (card 0, MLD_IN_STOCK), waste top (card 1, MLD_IN_WASTE),
    // waste non-top (card 2, MLD_IN_STOCK), tableau space (card 10, MLD_IN_SPACE).
    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[0]  = multiplicity_descriptor::make_locative(MLD_IN_STOCK);  // stock top
    descs[1]  = multiplicity_descriptor::make_locative(MLD_IN_WASTE);  // waste top
    descs[2]  = multiplicity_descriptor::make_locative(MLD_IN_STOCK);  // waste non-top
    descs[10] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);  // tableau bottom

    setup_engine(eng, descs);

    // Simulate make_move for count=1: card 0 dealt from stock to waste, then
    // immediately played to tableau (sits on card 10).
    // Pre-move waste top = card 1. Post-move waste top = card 1 (unchanged).
    // Changes: played (card 0) → predecessor(10, false).
    std::pair<uint8_t, multiplicity_descriptor> changes[1];
    changes[0] = {0, multiplicity_descriptor::make_predecessor(10, false)};
    eng.incremental_update_none(changes, 1);

    verify_matches_scratch(eng);
}

// Test: stock deal of 2 cards — old waste top moves down, new waste top promoted.
TEST(MultiplicityIncrementalTest, StockKPlus_Count2_WasteTopChanges) {
    multiplicity_descriptor_engine eng;

    // stock: cards 0 (top), 1. waste: card 2 (top=MLD_IN_WASTE), card 3 (MLD_IN_STOCK).
    // tableau space: card 20.
    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[0]  = multiplicity_descriptor::make_locative(MLD_IN_STOCK);
    descs[1]  = multiplicity_descriptor::make_locative(MLD_IN_STOCK);
    descs[2]  = multiplicity_descriptor::make_locative(MLD_IN_WASTE);  // pre-move waste top
    descs[3]  = multiplicity_descriptor::make_locative(MLD_IN_STOCK);
    descs[20] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);

    setup_engine(eng, descs);

    // Simulate make_move count=2:
    //   deal s1,s0 to waste → waste=[s1,s0,w2,w3,...]; played=s1 → tableau on card20.
    //   Post-move waste top = s0 (card 0). Old waste top = card 2 → moves to pos 2.
    // Changes:
    //   card 1 (played) → predecessor(20, false)
    //   card 2 (old top) → MLD_IN_STOCK (moved to pos 2)
    //   card 0 (new top) → MLD_IN_WASTE
    std::pair<uint8_t, multiplicity_descriptor> changes[3];
    changes[0] = {1, multiplicity_descriptor::make_predecessor(20, false)};
    changes[1] = {2, multiplicity_descriptor::make_locative(MLD_IN_STOCK)};
    changes[2] = {0, multiplicity_descriptor::make_locative(MLD_IN_WASTE)};
    eng.incremental_update_none(changes, 3);

    verify_matches_scratch(eng);
}

// Test: undo of count=2 deal — reverses the descriptor changes.
TEST(MultiplicityIncrementalTest, StockKPlus_UndoCount2) {
    multiplicity_descriptor_engine eng;

    // Post-make state (after count=2 forward move from test above):
    //   card 1: predecessor(20, false)   — played card at tableau
    //   card 2: MLD_IN_STOCK             — former waste top, now at interior
    //   card 0: MLD_IN_WASTE             — new waste top
    //   card 3: MLD_IN_STOCK             — waste non-top
    //   card 20: MLD_IN_SPACE
    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[1]  = multiplicity_descriptor::make_predecessor(20, false);
    descs[2]  = multiplicity_descriptor::make_locative(MLD_IN_STOCK);
    descs[0]  = multiplicity_descriptor::make_locative(MLD_IN_WASTE);
    descs[3]  = multiplicity_descriptor::make_locative(MLD_IN_STOCK);
    descs[20] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);

    setup_engine(eng, descs);

    // Simulate undo_move count=2:
    //   pre_undo_played = card 1, pre_undo_waste_top = card 0 (MLD_IN_WASTE).
    //   After undo: waste top = card 2 (MLD_IN_WASTE), card 0 → MLD_IN_STOCK, card 1 → MLD_IN_STOCK.
    // Changes:
    //   card 1 (played) → MLD_IN_STOCK
    //   card 2 (post-undo waste top) → MLD_IN_WASTE
    //   card 0 (pre-undo waste top, now interior) → MLD_IN_STOCK
    std::pair<uint8_t, multiplicity_descriptor> changes[3];
    changes[0] = {1, multiplicity_descriptor::make_locative(MLD_IN_STOCK)};
    changes[1] = {2, multiplicity_descriptor::make_locative(MLD_IN_WASTE)};
    changes[2] = {0, multiplicity_descriptor::make_locative(MLD_IN_STOCK)};
    eng.incremental_update_none(changes, 3);

    verify_matches_scratch(eng);
}

// Test: stock deal from empty waste — no old waste top to demote.
TEST(MultiplicityIncrementalTest, StockKPlus_EmptyWasteBefore) {
    multiplicity_descriptor_engine eng;

    // stock: cards 0, 1 (MLD_IN_STOCK). waste: empty. tableau: card 20.
    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[0]  = multiplicity_descriptor::make_locative(MLD_IN_STOCK);
    descs[1]  = multiplicity_descriptor::make_locative(MLD_IN_STOCK);
    descs[20] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);

    setup_engine(eng, descs);

    // Simulate count=2 deal from empty waste:
    //   waste=[s1,s0], played=s1 → tableau. Post-move waste top = s0 (card 0).
    // Changes: played (card 1) → tableau; new waste top (card 0) → MLD_IN_WASTE.
    std::pair<uint8_t, multiplicity_descriptor> changes[2];
    changes[0] = {1, multiplicity_descriptor::make_predecessor(20, false)};
    changes[1] = {0, multiplicity_descriptor::make_locative(MLD_IN_WASTE)};
    eng.incremental_update_none(changes, 2);

    verify_matches_scratch(eng);
}

// Test: count=-1 (return card from waste to stock, play new waste top).
// Old waste top (card 0) moves to stock; played (card 1) goes to tableau;
// new waste top (card 2) promoted to MLD_IN_WASTE.
TEST(MultiplicityIncrementalTest, StockKPlus_NegativeCount) {
    multiplicity_descriptor_engine eng;

    // waste: card 0 (top=MLD_IN_WASTE), card 1 (MLD_IN_STOCK), card 2 (MLD_IN_STOCK).
    // tableau: card 20.
    multiplicity_descriptor descs[52];
    for (uint8_t c = 0; c < 52; c++)
        descs[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    descs[0]  = multiplicity_descriptor::make_locative(MLD_IN_WASTE);
    descs[1]  = multiplicity_descriptor::make_locative(MLD_IN_STOCK);
    descs[2]  = multiplicity_descriptor::make_locative(MLD_IN_STOCK);
    descs[20] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);

    setup_engine(eng, descs);

    // Simulate count=-1: card 0 moves from waste to stock; played=card 1 → tableau.
    // New waste top = card 2.
    // Changes: card 1 (played) → tableau; card 0 (old top) → MLD_IN_STOCK; card 2 → MLD_IN_WASTE.
    std::pair<uint8_t, multiplicity_descriptor> changes[3];
    changes[0] = {1, multiplicity_descriptor::make_predecessor(20, false)};
    changes[1] = {0, multiplicity_descriptor::make_locative(MLD_IN_STOCK)};
    changes[2] = {2, multiplicity_descriptor::make_locative(MLD_IN_WASTE)};
    eng.incremental_update_none(changes, 3);

    verify_matches_scratch(eng);
}
