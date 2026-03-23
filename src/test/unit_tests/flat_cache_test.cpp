#include <gtest/gtest.h>
#include "../../main/game/flat_cache.h"
#include "../../main/game/zobrist.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"

class FlatCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }
    sol_rules rules = rules_parser::from_preset("free-cell");
};

// Test 1: BasicInsertAndContains
TEST_F(FlatCacheTest, BasicInsertAndContains) {
    flat_cache cache(1000);
    game_state gs1(rules, 1, game_state::streamliner_options::NONE);

    // Initial state
    EXPECT_TRUE(cache.insert(gs1));
    EXPECT_TRUE(cache.contains(gs1));
    EXPECT_EQ(cache.size(), 1);

    // Make a move to get a different state
    auto moves = gs1.get_legal_moves();
    ASSERT_FALSE(moves.empty());
    gs1.make_move(moves[0]);

    EXPECT_FALSE(cache.contains(gs1));
    EXPECT_TRUE(cache.insert(gs1));
    EXPECT_TRUE(cache.contains(gs1));
    EXPECT_EQ(cache.size(), 2);
}

// Test 2: DuplicateInsertReturnsFalse
TEST_F(FlatCacheTest, DuplicateInsertReturnsFalse) {
    flat_cache cache(1000);
    game_state gs(rules, 42, game_state::streamliner_options::NONE);

    EXPECT_TRUE(cache.insert(gs));
    EXPECT_FALSE(cache.insert(gs));
    EXPECT_EQ(cache.size(), 1);
}

// Test 3: ContainsReturnsFalseForAbsent
TEST_F(FlatCacheTest, ContainsReturnsFalseForAbsent) {
    flat_cache cache(1000);
    game_state gs1(rules, 1, game_state::streamliner_options::NONE);
    game_state gs2(rules, 1, game_state::streamliner_options::NONE);
    
    // Make a move in gs2 to make it different.
    auto moves = gs2.get_legal_moves();
    ASSERT_FALSE(moves.empty());
    gs2.make_move(moves[0]);

    cache.insert(gs1);
    EXPECT_TRUE(cache.contains(gs1));
    EXPECT_FALSE(cache.contains(gs2));
}

// Test 4: SizeTracking
TEST_F(FlatCacheTest, SizeTracking) {
    flat_cache cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);
    
    EXPECT_TRUE(cache.insert(gs));
    for (int i = 0; i < 100; ++i) {
        auto moves = gs.get_legal_moves();
        if (moves.empty()) break;
        gs.make_move(moves[0]);
        // Some moves might lead back to known states or symmetry-equivalent states, 
        // so we don't expect insert to always be true.
        cache.insert(gs);
    }
    EXPECT_GT(cache.size(), 1);
}

// Test 5: EvictionWorks
TEST_F(FlatCacheTest, EvictionWorks) {
    // 1 cluster = 2 slots
    flat_cache cache(2);
    
    game_state gs(rules, 1, game_state::streamliner_options::NONE);
    cache.insert(gs);

    for (int i = 0; i < 200; ++i) {
        auto moves = gs.get_legal_moves();
        if (moves.empty()) break;
        gs.make_move(moves[i % moves.size()]);
        cache.insert(gs);
    }

    // With 2 slots and many unique states, we MUST have evictions.
    EXPECT_GT(cache.get_states_removed_from_cache(), 0);
    EXPECT_LE(cache.size(), 2);
}

// Test 6: ClearResetsEverything
TEST_F(FlatCacheTest, ClearResetsEverything) {
    flat_cache cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);
    
    cache.insert(gs);
    EXPECT_EQ(cache.size(), 1);
    
    cache.clear();
    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.get_states_removed_from_cache(), 0);
    EXPECT_FALSE(cache.contains(gs));
}

// Test 7: StressTest
TEST_F(FlatCacheTest, StressTest) {
    flat_cache cache(10000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);
    
    for (int i = 0; i < 1000; ++i) {
        auto moves = gs.get_legal_moves();
        if (moves.empty()) break;
        gs.make_move(moves[i % moves.size()]);
        cache.insert(gs);
    }
    EXPECT_LE(cache.size(), cache.bucket_count());
}
