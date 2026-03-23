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

// Test 2: Verify Solver outcome consistency (seeds 1-10) using BlackHole
TEST_F(SolverCacheSelectionTest, SolverWithFlatCacheProducesSameOutcome) {
    sol_rules rules = rules_parser::from_preset("black-hole");
    uint64_t cache_capacity = 100000;

    for (int seed = 1; seed <= 10; ++seed) {
        game_state gs_lru(rules, seed, game_state::streamliner_options::NONE);
        game_state gs_flat(rules, seed, game_state::streamliner_options::NONE);

        // Solve with LRU
        lru_cache cache_lru(gs_lru, cache_capacity);
        solver sol_lru(gs_lru, cache_lru);
        solver::result res_lru = sol_lru.run(boost::optional<std::chrono::milliseconds>(10000));

        // Solve with Flat
        flat_cache cache_flat(cache_capacity);
        solver sol_flat(gs_flat, cache_flat);
        solver::result res_flat = sol_flat.run(boost::optional<std::chrono::milliseconds>(10000));

        EXPECT_EQ(res_lru.sol_type, res_flat.sol_type) << "Outcome mismatch for seed " << seed;
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
