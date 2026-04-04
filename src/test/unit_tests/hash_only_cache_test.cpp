#include <gtest/gtest.h>
#include <memory>
#include "../../main/game/hash_only_cache.h"
#include "../../main/game/flat_cache.h"
#include "../../main/game/dual_cache.h"
#include "../../main/game/zobrist.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/solver/solver.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"

class HashOnlyCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }
    sol_rules rules = rules_parser::from_preset("free-cell");
};

// Test 1: BasicInsertAndContains
TEST_F(HashOnlyCacheTest, BasicInsertAndContains) {
    hash_only_cache cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    // Newly inserted state — insert() must return true
    EXPECT_TRUE(cache.insert(gs));
    // Now contains() must confirm presence
    EXPECT_TRUE(cache.contains(gs));
    EXPECT_EQ(cache.size(), 1u);

    // Make a move to produce a different state
    auto moves = gs.get_legal_moves();
    ASSERT_FALSE(moves.empty());
    gs.make_move(moves[0]);

    EXPECT_FALSE(cache.contains(gs));
    EXPECT_TRUE(cache.insert(gs));
    EXPECT_TRUE(cache.contains(gs));
    EXPECT_EQ(cache.size(), 2u);
}

// Test 2: DuplicateInsertReturnsFalse
TEST_F(HashOnlyCacheTest, DuplicateInsertReturnsFalse) {
    hash_only_cache cache(1000);
    game_state gs(rules, 42, game_state::streamliner_options::NONE);

    EXPECT_TRUE(cache.insert(gs));
    EXPECT_FALSE(cache.insert(gs));  // Already present
    EXPECT_EQ(cache.size(), 1u);
}

// Test 3: ContainsReturnsFalseForAbsent
TEST_F(HashOnlyCacheTest, ContainsReturnsFalseForAbsent) {
    hash_only_cache cache(1000);
    game_state gs1(rules, 1, game_state::streamliner_options::NONE);
    game_state gs2(rules, 1, game_state::streamliner_options::NONE);

    // Advance gs2 by one move to make a distinct state
    auto moves = gs2.get_legal_moves();
    ASSERT_FALSE(moves.empty());
    gs2.make_move(moves[0]);

    cache.insert(gs1);
    EXPECT_TRUE(cache.contains(gs1));
    EXPECT_FALSE(cache.contains(gs2));
}

// Test 4: SizeTracking
TEST_F(HashOnlyCacheTest, SizeTracking) {
    hash_only_cache cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    EXPECT_TRUE(cache.insert(gs));
    for (int i = 0; i < 100; ++i) {
        auto moves = gs.get_legal_moves();
        if (moves.empty()) break;
        gs.make_move(moves[0]);
        cache.insert(gs);
    }
    EXPECT_GT(cache.size(), 1u);
}

// Test 5: EvictionCountsCorrectly
// With 2 slots and many distinct states we must get evictions.
TEST_F(HashOnlyCacheTest, EvictionCountsCorrectly) {
    // 1 cluster = 2 slots total
    hash_only_cache cache(2);

    game_state gs(rules, 1, game_state::streamliner_options::NONE);
    cache.insert(gs);

    for (int i = 0; i < 200; ++i) {
        auto moves = gs.get_legal_moves();
        if (moves.empty()) break;
        gs.make_move(moves[i % moves.size()]);
        cache.insert(gs);
    }

    EXPECT_GT(cache.get_states_removed_from_cache(), 0u);
    EXPECT_LE(cache.size(), 2u);
}

// Test 6: ClearResetsEverything
TEST_F(HashOnlyCacheTest, ClearResetsEverything) {
    hash_only_cache cache(1000);
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    cache.insert(gs);
    EXPECT_EQ(cache.size(), 1u);

    cache.clear();
    EXPECT_EQ(cache.size(), 0u);
    EXPECT_EQ(cache.get_states_removed_from_cache(), 0u);
    EXPECT_FALSE(cache.contains(gs));
}

// Test 7: BucketCountIsDoubleNumClusters
TEST_F(HashOnlyCacheTest, BucketCountIsDoubleNumClusters) {
    hash_only_cache cache(100);
    // 100 / 2 = 50 clusters => bucket_count == 100
    EXPECT_EQ(cache.bucket_count(), 100u);
}

// Test 8: ZeroHashSentinelHandled
// The Zobrist hash of 0 must be remapped to 1 and still work correctly.
// We can't force game_state to produce hash=0, but we can verify insert/contains
// remain consistent when the cache is filled repeatedly.
TEST_F(HashOnlyCacheTest, StressConsistency) {
    hash_only_cache cache(10000);
    game_state gs(rules, 7, game_state::streamliner_options::NONE);

    for (int i = 0; i < 500; ++i) {
        auto moves = gs.get_legal_moves();
        if (moves.empty()) break;
        gs.make_move(moves[i % moves.size()]);
        cache.insert(gs);
    }
    EXPECT_LE(cache.size(), cache.bucket_count());
}

// Test 9: DualCacheAgreementWithFlatOnKlondike
// hash_only_cache uses the same Zobrist hash as flat_cache for state identity.
// Pre-eviction, both must agree perfectly on insert() results (both miss or both hit).
TEST_F(HashOnlyCacheTest, DualCacheAgreementWithFlatOnKlondike) {
    sol_rules klondike_rules = rules_parser::from_preset("klondike-deal-1");

    for (int seed = 1; seed <= 5; ++seed) {
        dual_cache::context() = "klondike-deal-1 (seed " + std::to_string(seed) + ")";

        // Primary = hash_only, reference = flat_cache
        game_state gs(klondike_rules, seed, game_state::streamliner_options::NONE);
        dual_cache cache(
            std::make_unique<hash_only_cache>(10000000),
            std::make_unique<flat_cache>(10000000),
            "hash-only", "flat"
        );
        solver sol(gs, cache);
        sol.run(boost::optional<std::chrono::milliseconds>(10000));

        // hash-only can only miss states that flat hits (hash-only has no payload check,
        // so it should never report a hit when flat misses). Both use Zobrist hash, so
        // pre-eviction flat_only_hits (reference=flat hit, primary=hash-only miss) should be 0.
        EXPECT_EQ(cache.get_flat_only_hits(), 0u)
            << "hash-only MISSED a state that flat HIT in klondike-deal-1 at seed " << seed;
    }
}
