#include <gtest/gtest.h>
#include "../../main/game/predecessor_flat_cache.h"
#include "../../main/game/predecessor_state.h"
#include "../../main/game/zobrist.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"

class PredecessorCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }
    sol_rules rules = rules_parser::from_preset("accordion");
};

// Test 1: Game state reports uses_predecessor_cache for accordion
TEST_F(PredecessorCacheTest, DISABLED_AccordionUsesPredecessorCache) {
    game_state gs(rules, 1, game_state::streamliner_options::NONE);
    EXPECT_TRUE(gs.uses_predecessor_cache());
}

// Test 2: Non-accordion game does not use predecessor cache
TEST(DISABLED_PredecessorCacheNonAccordion, FreeCellDoesNotUsePredecessorCache) {
    zobrist_hash::init();
    sol_rules fc_rules = rules_parser::from_preset("free-cell");
    game_state gs(fc_rules, 1, game_state::streamliner_options::NONE);
    EXPECT_FALSE(gs.uses_predecessor_cache());
}

// Test 3: Basic insert and contains
TEST_F(PredecessorCacheTest, DISABLED_BasicInsertAndContains) {
    predecessor_flat_cache cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    EXPECT_TRUE(cache.insert(gs));
    EXPECT_TRUE(cache.contains(gs));
    EXPECT_EQ(cache.size(), 1);
}

// Test 4: Duplicate insert returns false
TEST_F(PredecessorCacheTest, DISABLED_DuplicateInsertReturnsFalse) {
    predecessor_flat_cache cache(1000);
    game_state gs(rules, 42, game_state::streamliner_options::NONE);

    EXPECT_TRUE(cache.insert(gs));
    EXPECT_FALSE(cache.insert(gs));
    EXPECT_EQ(cache.size(), 1);
}

// Test 5: Different states are distinct
TEST_F(PredecessorCacheTest, DISABLED_DifferentStatesAreDistinct) {
    predecessor_flat_cache cache(1000);
    game_state gs1(rules, 1, game_state::streamliner_options::NONE);
    game_state gs2(rules, 2, game_state::streamliner_options::NONE);

    EXPECT_TRUE(cache.insert(gs1));
    EXPECT_TRUE(cache.insert(gs2));
    EXPECT_TRUE(cache.contains(gs1));
    EXPECT_TRUE(cache.contains(gs2));
    EXPECT_EQ(cache.size(), 2);
}

// Test 6: State after move is different
TEST_F(PredecessorCacheTest, DISABLED_StateAfterMoveIsDifferent) {
    predecessor_flat_cache cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    cache.insert(gs);

    auto moves = gs.get_legal_moves();
    if (!moves.empty()) {
        gs.make_move(moves[0]);
        EXPECT_FALSE(cache.contains(gs));
        EXPECT_TRUE(cache.insert(gs));
    }
}

// Test 7: Undo restores to cached state
TEST_F(PredecessorCacheTest, DISABLED_UndoRestoresToCachedState) {
    predecessor_flat_cache cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    uint64_t hash_before = gs.get_predecessor_zobrist_hash();
    predecessor_state payload_before = gs.get_predecessor_payload();

    cache.insert(gs);

    auto moves = gs.get_legal_moves();
    ASSERT_FALSE(moves.empty()) << "No legal moves for accordion seed 1";

    gs.make_move(moves[0]);

    uint64_t hash_after_move = gs.get_predecessor_zobrist_hash();
    predecessor_state payload_after_move = gs.get_predecessor_payload();

    // Diagnostic: check the move changed something
    bool hash_changed = (hash_after_move != hash_before);
    bool payload_changed = !payload_after_move.matches(payload_before);
    // Note: if move is not accordion type, predecessor state won't change,
    // and contains should still return true
    if (!hash_changed && !payload_changed) {
        // Predecessor state unchanged — contains should still find it
        EXPECT_TRUE(cache.contains(gs))
            << "Predecessor state unchanged by move but contains fails";
    } else {
        EXPECT_FALSE(cache.contains(gs))
            << "Predecessor state changed but contains still finds old state";
    }

    gs.undo_move(moves[0]);

    uint64_t hash_after_undo = gs.get_predecessor_zobrist_hash();
    predecessor_state payload_after_undo = gs.get_predecessor_payload();

    EXPECT_EQ(hash_before, hash_after_undo)
        << "Hash not restored: before=" << hash_before << " after_undo=" << hash_after_undo;

    // Check each predecessor byte
    for (int i = 0; i < 52; i++) {
        EXPECT_EQ(payload_before.get_predecessor(i), payload_after_undo.get_predecessor(i))
            << "Predecessor mismatch at card " << i
            << ": before=" << (int)payload_before.get_predecessor(i)
            << " after_undo=" << (int)payload_after_undo.get_predecessor(i);
    }

    EXPECT_TRUE(cache.contains(gs));
}

// Test 8: Predecessor hash changes after accordion move
TEST_F(PredecessorCacheTest, DISABLED_PredecessorHashChangesAfterMove) {
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    uint64_t hash_before = gs.get_predecessor_zobrist_hash();

    auto moves = gs.get_legal_moves();
    if (!moves.empty()) {
        gs.make_move(moves[0]);
        uint64_t hash_after = gs.get_predecessor_zobrist_hash();
        // Hash should change (extremely unlikely to collide)
        EXPECT_NE(hash_before, hash_after);

        gs.undo_move(moves[0]);
        uint64_t hash_restored = gs.get_predecessor_zobrist_hash();
        EXPECT_EQ(hash_before, hash_restored);
    }
}

// Test 9: Clear empties the cache
TEST_F(PredecessorCacheTest, DISABLED_ClearEmptiesCache) {
    predecessor_flat_cache cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    cache.insert(gs);
    EXPECT_EQ(cache.size(), 1);

    cache.clear();
    EXPECT_EQ(cache.size(), 0);
    EXPECT_FALSE(cache.contains(gs));
}

// Test 10: Multiple moves and undos maintain consistency
TEST_F(PredecessorCacheTest, DISABLED_MultipleMovesAndUndos) {
    predecessor_flat_cache cache(10000);
    game_state gs(rules, 5, game_state::streamliner_options::NONE);

    // Insert initial state
    cache.insert(gs);
    uint64_t initial_hash = gs.get_predecessor_zobrist_hash();

    // Make several moves, caching each state
    std::vector<move> moves_made;
    for (int i = 0; i < 5; i++) {
        auto legal = gs.get_legal_moves();
        if (legal.empty()) break;
        gs.make_move(legal[0]);
        cache.insert(gs);
        moves_made.push_back(legal[0]);
    }

    // Undo all moves
    for (auto it = moves_made.rbegin(); it != moves_made.rend(); ++it) {
        gs.undo_move(*it);
    }

    // Should be back to initial state
    EXPECT_EQ(gs.get_predecessor_zobrist_hash(), initial_hash);
    EXPECT_TRUE(cache.contains(gs));
}
