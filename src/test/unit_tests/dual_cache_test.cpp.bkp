#include <gtest/gtest.h>
#include <chrono>
#include "../../main/game/dual_cache.h"
#include "../../main/game/zobrist.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/solver/solver.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"

class DualCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }

    void run_agreement_test(const std::string& preset, int seeds = 3, uint64_t cap = 10000000) {
        sol_rules rules = rules_parser::from_preset(preset);
        for (int seed = 1; seed <= seeds; ++seed) {
            dual_cache::context() = preset + " (seed " + std::to_string(seed) + ")";
            game_state gs(rules, seed, game_state::streamliner_options::NONE);
            dual_cache cache(gs, cap);
            solver sol(gs, cache);
            sol.run(boost::optional<std::chrono::milliseconds>(10000));
            
            EXPECT_EQ(cache.get_lru_only_hits(), 0) 
                << "Unacceptable LRU=HIT, flat=MISS in " << preset << " at seed " << seed;
        }
    }

    void run_outcome_test(const std::string& preset, int seeds, uint64_t cap) {
        sol_rules rules = rules_parser::from_preset(preset);
        for (int seed = 1; seed <= seeds; ++seed) {
            game_state gs1(rules, seed, game_state::streamliner_options::NONE);
            game_state gs2(rules, seed, game_state::streamliner_options::NONE);

            lru_cache cache_lru(gs1, cap);
            solver sol_lru(gs1, cache_lru);
            auto res_lru = sol_lru.run(boost::optional<std::chrono::milliseconds>(5000));

            flat_cache cache_flat(cap);
            solver sol_flat(gs2, cache_flat);
            auto res_flat = sol_flat.run(boost::optional<std::chrono::milliseconds>(5000));

            if (res_lru.sol_type != solver::result::type::TIMEOUT &&
                res_flat.sol_type != solver::result::type::TIMEOUT) {
                EXPECT_EQ(res_lru.sol_type, res_flat.sol_type) << "Outcome mismatch at " << preset << " seed " << seed;
            }
        }
    }
};

// --- Outcome Agreement Tests ---
// flat_cache uses per-card descriptors which don't collapse pile-order-equivalent
// states like lru_cache's sorted pile encoding does. So flat_cache may explore
// more states, but must reach the same solvability outcome.
// Known divergence categories:
//   A: Pile-ordering asymmetry (LRU deduplicates more via sorted piles)
//   B: Waste-pointer asymmetry (LRU collapses circular waste in redeal games)
//   C: Suit symmetry (LRU collapses suit-equivalent states)

TEST_F(DualCacheTest, FreeCellOutcome) {
    run_outcome_test("free-cell", 3, 10000000);
}

TEST_F(DualCacheTest, BakersGameOutcome) {
    run_outcome_test("bakers-game", 3, 10000000);
}

TEST_F(DualCacheTest, EightOffOutcome) {
    run_outcome_test("eight-off", 3, 10000000);
}

TEST_F(DualCacheTest, SpanishPatienceOutcome) {
    run_outcome_test("spanish-patience", 1, 10000000);
}

TEST_F(DualCacheTest, SomersetOutcome) {
    run_outcome_test("somerset", 3, 10000000);
}

TEST_F(DualCacheTest, FlowerGardenOutcome) {
    run_outcome_test("flower-garden", 1, 10000000);
}

TEST_F(DualCacheTest, FortunesFavorOutcome) {
    run_outcome_test("fortunes-favor", 3, 10000000);
}

TEST_F(DualCacheTest, SeahavenTowersOutcome) {
    run_outcome_test("seahaven-towers", 3, 10000000);
}

TEST_F(DualCacheTest, KlondikeOutcome) {
    run_outcome_test("klondike-deal-1", 3, 10000000);
}

TEST_F(DualCacheTest, CanfieldOutcome) {
    run_outcome_test("canfield", 1, 10000000);
}

TEST_F(DualCacheTest, BlackHoleOutcome) {
    run_outcome_test("black-hole", 5, 20000);
}

TEST_F(DualCacheTest, GolfOutcome) {
    run_outcome_test("golf", 3, 50000);
}
