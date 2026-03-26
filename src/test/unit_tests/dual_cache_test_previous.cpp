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

// --- Node Agreement Tests (Streamliner: NONE) ---
// These games do NOT have a hole and we use no streamliners, 
// so legacy symmetry should be disabled. Nodes must match exactly.

TEST_F(DualCacheTest, FreeCellAgreement) {
    run_agreement_test("free-cell");
}

TEST_F(DualCacheTest, BakersGameAgreement) {
    run_agreement_test("bakers-game");
}

TEST_F(DualCacheTest, EightOffAgreement) {
    run_agreement_test("eight-off");
}

TEST_F(DualCacheTest, SpanishPatienceAgreement) {
    run_agreement_test("spanish-patience", 1); 
}

TEST_F(DualCacheTest, SomersetAgreement) {
    run_agreement_test("somerset", 3);
}

TEST_F(DualCacheTest, FlowerGardenAgreement) {
    run_agreement_test("flower-garden", 1);
}

TEST_F(DualCacheTest, FortunesFavorAgreement) {
    run_agreement_test("fortunes-favor", 3);
}

TEST_F(DualCacheTest, SeahavenTowersAgreement) {
    run_agreement_test("seahaven-towers", 3);
}

// Games using outcome agreement tests because of valid diverging optimizations
TEST_F(DualCacheTest, KlondikeOutcomeAgreement) {
    run_outcome_test("klondike-deal-1", 3, 10000000);
}

TEST_F(DualCacheTest, CanfieldOutcomeAgreement) {
    run_outcome_test("canfield", 1, 10000000);
}

TEST_F(DualCacheTest, BlackHoleOutcomeOnly) {
    run_outcome_test("black-hole", 5, 20000);
}

TEST_F(DualCacheTest, GolfOutcomeOnly) {
    run_outcome_test("golf", 3, 50000);
}
