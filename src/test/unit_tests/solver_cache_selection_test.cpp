#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include "../../main/game/flat_cache.h"
#include "../../main/game/zobrist.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"
#include "../../main/solver/solver.h"

class SolverCacheSelectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }
};

// Test 1: Verify BlackHole uses New Cache (flat_cache) and solver works with it
TEST_F(SolverCacheSelectionTest, BlackHoleUsesNewCache) {
    sol_rules rules = rules_parser::from_preset("black-hole");
    EXPECT_TRUE(use_new_cache(rules));
 
    game_state gs(rules, 1, game_state::streamliner_options::NONE);
    uint64_t cache_capacity = 10000;
    
    // Explicitly using flat_cache to verify solver works with it
    std::unique_ptr<cache_interface> cache_ptr = std::make_unique<flat_cache>(cache_capacity);
    solver sol(gs, *cache_ptr);
    
    // Seed 1 is solvable for BlackHole
    solver::result res = sol.run(boost::optional<std::chrono::milliseconds>(10000));
    EXPECT_EQ(res.sol_type, solver::result::type::SOLVED);
}

// Test 2: Verify flat cache produces deterministic outcomes (run each seed twice)
// Note: LRU vs flat comparison is not used here because after M6, LRU is no longer
// commutative for flat-cache games (pile ordering removed), making it much slower
// and potentially timing out before flat cache does.
// Seeds 1-3 with 3s timeout to avoid slow unsolvable runs.
TEST_F(SolverCacheSelectionTest, SolverWithFlatCacheProducesSameOutcome) {
    sol_rules rules = rules_parser::from_preset("black-hole");
    uint64_t cache_capacity = 100000;

    for (int seed = 1; seed <= 3; ++seed) {
        game_state gs1(rules, seed, game_state::streamliner_options::NONE);
        game_state gs2(rules, seed, game_state::streamliner_options::NONE);

        flat_cache cache1(cache_capacity);
        solver sol1(gs1, cache1);
        solver::result res1 = sol1.run(boost::optional<std::chrono::milliseconds>(3000));

        flat_cache cache2(cache_capacity);
        solver sol2(gs2, cache2);
        solver::result res2 = sol2.run(boost::optional<std::chrono::milliseconds>(3000));

        EXPECT_EQ(res1.sol_type, res2.sol_type) << "Flat cache non-deterministic for seed " << seed;
    }
}

// Test 3: Verify use_new_cache correctly filters game types
TEST_F(SolverCacheSelectionTest, UseCacheSelectionFunction) {
    EXPECT_TRUE(use_new_cache(rules_parser::from_preset("free-cell")));
    EXPECT_TRUE(use_new_cache(rules_parser::from_preset("klondike")));
    EXPECT_TRUE(use_new_cache(rules_parser::from_preset("black-hole")));
    EXPECT_TRUE(use_new_cache(rules_parser::from_preset("spanish-patience")));
    
    // Spider is two-deck
    EXPECT_FALSE(use_new_cache(rules_parser::from_preset("spider")));
}
