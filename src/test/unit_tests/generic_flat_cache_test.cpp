// generic_flat_cache_test.cpp
//
// P2-B unit tests for generic_flat_cache<Policy> for all three specialisations.
// Each suite mirrors the corresponding per-class test file so parity can be
// verified by inspection:
//   CompactStatePolicy  ↔  flat_cache_test.cpp
//   HashOnlyPolicy      ↔  hash_only_cache_test.cpp
//   PredecessorPolicy   ↔  predecessor_cache_test.cpp
//
// Only the insert/contains/eviction/clear surface of generic_flat_cache itself
// is exercised here.  Predecessor payload semantics are covered by the existing
// predecessor_cache_test.cpp and are not duplicated.
//
// KI-7 note: PredecessorPolicy tests use accordion game_states.  If a test
// fails with the same crash signature as PredecessorDualCacheTest.AccordionAgreement,
// it is the pre-existing KI-7 — do not investigate.  Any other new failure is
// a blocker.

#include <gtest/gtest.h>
#include "../../main/game/generic_flat_cache.h"
#include "../../main/game/zobrist.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"

// ─── Suite 1: CompactStatePolicy ─────────────────────────────────────────────

class GenericCompactStateCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }
    sol_rules rules = rules_parser::from_preset("free-cell");
};

TEST_F(GenericCompactStateCacheTest, BasicInsertAndContains) {
    generic_flat_cache<CompactStatePolicy> cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    EXPECT_TRUE(cache.insert(gs));
    EXPECT_TRUE(cache.contains(gs));
    EXPECT_EQ(cache.size(), 1);

    auto moves = gs.get_legal_moves();
    ASSERT_FALSE(moves.empty());
    gs.make_move(moves[0]);

    EXPECT_FALSE(cache.contains(gs));
    EXPECT_TRUE(cache.insert(gs));
    EXPECT_TRUE(cache.contains(gs));
    EXPECT_EQ(cache.size(), 2);
}

TEST_F(GenericCompactStateCacheTest, DuplicateInsertReturnsFalse) {
    generic_flat_cache<CompactStatePolicy> cache(1000);
    game_state gs(rules, 42, game_state::streamliner_options::NONE);

    EXPECT_TRUE(cache.insert(gs));
    EXPECT_FALSE(cache.insert(gs));
    EXPECT_EQ(cache.size(), 1);
}

TEST_F(GenericCompactStateCacheTest, DifferentStatesAreDistinct) {
    generic_flat_cache<CompactStatePolicy> cache(1000);
    game_state gs1(rules, 1, game_state::streamliner_options::NONE);
    game_state gs2(rules, 2, game_state::streamliner_options::NONE);

    cache.insert(gs1);
    EXPECT_TRUE(cache.contains(gs1));
    EXPECT_FALSE(cache.contains(gs2));
}

TEST_F(GenericCompactStateCacheTest, SizeTracking) {
    generic_flat_cache<CompactStatePolicy> cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    cache.insert(gs);
    for (int i = 0; i < 100; ++i) {
        auto moves = gs.get_legal_moves();
        if (moves.empty()) break;
        gs.make_move(moves[0]);
        cache.insert(gs);
    }
    EXPECT_GT(cache.size(), 1);
}

TEST_F(GenericCompactStateCacheTest, EvictionWorks) {
    generic_flat_cache<CompactStatePolicy> cache(2);  // 1 cluster = 2 slots
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    cache.insert(gs);
    for (int i = 0; i < 200; ++i) {
        auto moves = gs.get_legal_moves();
        if (moves.empty()) break;
        gs.make_move(moves[i % static_cast<int>(moves.size())]);
        cache.insert(gs);
    }

    EXPECT_GT(cache.get_states_removed_from_cache(), 0);
    EXPECT_LE(cache.size(), 2);
}

TEST_F(GenericCompactStateCacheTest, ClearResetsEverything) {
    generic_flat_cache<CompactStatePolicy> cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    cache.insert(gs);
    EXPECT_EQ(cache.size(), 1);

    cache.clear();
    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.get_states_removed_from_cache(), 0);
    EXPECT_FALSE(cache.contains(gs));
}

TEST_F(GenericCompactStateCacheTest, ClusterSizeMatchesFlatCache) {
    EXPECT_EQ(sizeof(generic_flat_cache<CompactStatePolicy>::cluster), 64u);
}

// ─── Suite 2: HashOnlyPolicy ──────────────────────────────────────────────────

class GenericHashOnlyCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }
    sol_rules rules = rules_parser::from_preset("free-cell");
};

TEST_F(GenericHashOnlyCacheTest, BasicInsertAndContains) {
    generic_flat_cache<HashOnlyPolicy> cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    EXPECT_TRUE(cache.insert(gs));
    EXPECT_TRUE(cache.contains(gs));
    EXPECT_EQ(cache.size(), 1);

    auto moves = gs.get_legal_moves();
    ASSERT_FALSE(moves.empty());
    gs.make_move(moves[0]);

    EXPECT_FALSE(cache.contains(gs));
    EXPECT_TRUE(cache.insert(gs));
    EXPECT_TRUE(cache.contains(gs));
    EXPECT_EQ(cache.size(), 2);
}

TEST_F(GenericHashOnlyCacheTest, DuplicateInsertReturnsFalse) {
    generic_flat_cache<HashOnlyPolicy> cache(1000);
    game_state gs(rules, 42, game_state::streamliner_options::NONE);

    EXPECT_TRUE(cache.insert(gs));
    EXPECT_FALSE(cache.insert(gs));
    EXPECT_EQ(cache.size(), 1);
}

TEST_F(GenericHashOnlyCacheTest, DifferentStatesAreDistinct) {
    generic_flat_cache<HashOnlyPolicy> cache(1000);
    game_state gs1(rules, 1, game_state::streamliner_options::NONE);
    game_state gs2(rules, 2, game_state::streamliner_options::NONE);

    cache.insert(gs1);
    EXPECT_TRUE(cache.contains(gs1));
    EXPECT_FALSE(cache.contains(gs2));
}

TEST_F(GenericHashOnlyCacheTest, EvictionWorks) {
    generic_flat_cache<HashOnlyPolicy> cache(2);  // 1 cluster = 2 slots
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    cache.insert(gs);
    for (int i = 0; i < 200; ++i) {
        auto moves = gs.get_legal_moves();
        if (moves.empty()) break;
        gs.make_move(moves[i % static_cast<int>(moves.size())]);
        cache.insert(gs);
    }

    EXPECT_GT(cache.get_states_removed_from_cache(), 0);
    EXPECT_LE(cache.size(), 2);
}

TEST_F(GenericHashOnlyCacheTest, ClearResetsEverything) {
    generic_flat_cache<HashOnlyPolicy> cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    cache.insert(gs);
    EXPECT_EQ(cache.size(), 1);

    cache.clear();
    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.get_states_removed_from_cache(), 0);
    EXPECT_FALSE(cache.contains(gs));
}

TEST_F(GenericHashOnlyCacheTest, ClusterSizeMatchesHashOnlyCache) {
    EXPECT_EQ(sizeof(generic_flat_cache<HashOnlyPolicy>::cluster), 16u);
}

// ─── Suite 3: PredecessorPolicy ───────────────────────────────────────────────

class GenericPredecessorCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }
    sol_rules rules = rules_parser::from_preset("accordion");
};

TEST_F(GenericPredecessorCacheTest, BasicInsertAndContains) {
    generic_flat_cache<PredecessorPolicy> cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    EXPECT_TRUE(cache.insert(gs));
    EXPECT_TRUE(cache.contains(gs));
    EXPECT_EQ(cache.size(), 1);
}

TEST_F(GenericPredecessorCacheTest, DuplicateInsertReturnsFalse) {
    generic_flat_cache<PredecessorPolicy> cache(1000);
    game_state gs(rules, 42, game_state::streamliner_options::NONE);

    EXPECT_TRUE(cache.insert(gs));
    EXPECT_FALSE(cache.insert(gs));
    EXPECT_EQ(cache.size(), 1);
}

TEST_F(GenericPredecessorCacheTest, DifferentStatesAreDistinct) {
    generic_flat_cache<PredecessorPolicy> cache(1000);
    game_state gs1(rules, 1, game_state::streamliner_options::NONE);
    game_state gs2(rules, 2, game_state::streamliner_options::NONE);

    EXPECT_TRUE(cache.insert(gs1));
    EXPECT_TRUE(cache.insert(gs2));
    EXPECT_TRUE(cache.contains(gs1));
    EXPECT_TRUE(cache.contains(gs2));
    EXPECT_EQ(cache.size(), 2);
}

TEST_F(GenericPredecessorCacheTest, EvictionWorks) {
    generic_flat_cache<PredecessorPolicy> cache(2);  // 1 cluster = 2 slots
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    cache.insert(gs);
    for (int i = 0; i < 200; ++i) {
        auto moves = gs.get_legal_moves();
        if (moves.empty()) break;
        gs.make_move(moves[i % static_cast<int>(moves.size())]);
        cache.insert(gs);
    }

    EXPECT_GT(cache.get_states_removed_from_cache(), 0);
    EXPECT_LE(cache.size(), 2);
}

TEST_F(GenericPredecessorCacheTest, ClearResetsEverything) {
    generic_flat_cache<PredecessorPolicy> cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    cache.insert(gs);
    EXPECT_EQ(cache.size(), 1);

    cache.clear();
    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.get_states_removed_from_cache(), 0);
    EXPECT_FALSE(cache.contains(gs));
}

TEST_F(GenericPredecessorCacheTest, ClusterSizeMatchesPredecessorCache) {
    EXPECT_EQ(sizeof(generic_flat_cache<PredecessorPolicy>::cluster), 128u);
}
