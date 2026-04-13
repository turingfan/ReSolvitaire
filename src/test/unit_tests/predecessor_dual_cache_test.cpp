#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <boost/optional.hpp>
#include "../../main/game/dual_cache.h"
#include "../../main/game/predecessor_flat_cache.h"
#include "../../main/game/global_cache.h"
#include "../../main/game/zobrist.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/solver/solver.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"

class PredecessorDualCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }

    void run_predecessor_agreement_test(const std::string& preset, int seeds = 10, uint64_t cap = 1000000) {
        sol_rules rules = rules_parser::from_preset(preset);
        for (int seed = 1; seed <= seeds; ++seed) {
            dual_cache::context() = preset + " (seed " + std::to_string(seed) + ")";
            game_state gs(rules, seed, game_state::streamliner_options::NONE);
            
            // Accordion games automatically enable predecessor tracking.
            // predecessor_flat_cache correctly identifies itself via uses_predecessor_cache()
            // and the game state provides the necessary predecessor payload.
            
            dual_cache cache(
                std::make_unique<predecessor_flat_cache>(cap),
                std::make_unique<lru_cache>(gs, cap),
                "predecessor_flat", "lru"
            );

            solver sol(gs, cache);
            sol.run(boost::optional<std::chrono::milliseconds>(5000));

            // Agreement check: we only care if LRU found something that our predecessor cache missed
            // (until significant eviction occurs which makes comparison noise-prone).
            EXPECT_EQ(cache.get_lru_only_hits(), 0) << "Predecessor cache missed a state that LRU identified as HIT in seed " << seed;
            EXPECT_EQ(cache.get_flat_only_hits(), 0) << "predecessor_flat found a state that LRU missed in seed " << seed;
        }
    }
};

TEST_F(PredecessorDualCacheTest, DISABLED_AccordionAgreement) {
    // Accordion is the primary game that uses the predecessor cache.
    // We check seeds 1-10 to ensure the encoding and incremental updates are correct.
    run_predecessor_agreement_test("accordion", 10);
}
