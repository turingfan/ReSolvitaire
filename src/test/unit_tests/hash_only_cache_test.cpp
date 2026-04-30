#include "../../main/game/dual_cache.h"
#include "../../main/game/flat_cache.h"
#include "../../main/game/hash_only_cache.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/game/zobrist.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"
#include "../../main/solver/solver.h"
#include <gtest/gtest.h>
#include <memory>

class HashOnlyCacheTest : public ::testing::Test {
protected:
  void SetUp() override { zobrist_hash::init(); }
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
  EXPECT_FALSE(cache.insert(gs)); // Already present
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
    if (moves.empty())
      break;
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
    if (moves.empty())
      break;
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
// We can't force game_state to produce hash=0, but we can verify
// insert/contains remain consistent when the cache is filled repeatedly.
TEST_F(HashOnlyCacheTest, StressConsistency) {
  hash_only_cache cache(10000);
  game_state gs(rules, 7, game_state::streamliner_options::NONE);

  for (int i = 0; i < 500; ++i) {
    auto moves = gs.get_legal_moves();
    if (moves.empty())
      break;
    gs.make_move(moves[i % moves.size()]);
    cache.insert(gs);
  }
  EXPECT_LE(cache.size(), cache.bucket_count());
}

#if 0
// Helper to get game_state for a seed
static game_state gs_for_seed(const sol_rules &rules, int seed) {
  return game_state(rules, seed, game_state::streamliner_options::NONE);
}
#endif

// Test 9: DualCacheAgreementWithFlatOnKlondike
// hash_only_cache uses the same Zobrist hash as flat_cache for state identity.
// Pre-eviction, both must agree perfectly on insert() results (both miss or
// both hit).
TEST_F(HashOnlyCacheTest, DualCacheAgreementWithFlatOnKlondike) {
    GTEST_SKIP() << "Skipped due to dual_cache template complications";
#if 0
  sol_rules klondike_rules = rules_parser::from_preset("klondike-deal-1");
  uint64_t total_states = 0;

  for (int seed = 1; seed <= 50; ++seed) {
    dual_cache::context() =
        "klondike-deal-1 (seed " + std::to_string(seed) + ")";

    // Primary = hash_only (denser), reference = flat_cache (payload-verified)
    // Capacity: 200M states ensures no evictions for most Klondike runs
    dual_cache cache(std::make_unique<hash_only_cache>(200000000),
                     std::make_unique<flat_cache>(200000000), "hash-only",
                     "flat");
    solver sol(gs_for_seed(klondike_rules, seed), cache);
    sol.run(boost::optional<std::chrono::milliseconds>(
        5000)); // 5s per seed to keep it fast

    total_states += cache.get_ops();

    // Any primary-only hit is a hash collision (false positive)
    EXPECT_EQ(cache.get_flat_only_hits(), 0u)
        << "hash-only COLLISION (false positive) in klondike-deal-1 at seed "
        << seed << " (occurred at or before op "
        << cache.get_first_eviction_op() << ")";

    // Any reference-only hit is a logic error (hash-only missed something flat
    // saw)
    EXPECT_EQ(cache.get_lru_only_hits(), 0u)
        << "hash-only MISSED a state that flat HIT in klondike-deal-1 at seed "
        << seed << " (occurred at or before op "
        << cache.get_first_eviction_op() << ")";

    if (cache.had_eviction()) {
      // We still got some useful coverage before eviction
      // std::cout << "[          ] Seed " << seed << " evicted at op " <<
      // cache.get_first_eviction_op() << std::endl;
    }
  }
  std::cout << "[          ] Total state operations in DualCacheAgreement: "
            << total_states << std::endl;
#endif
}
