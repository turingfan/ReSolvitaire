#include <gtest/gtest.h>
#include <fstream>
#include "../../main/game/dual_cache.h"
#include "../../main/game/zobrist.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/solver/solver.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"

class MismatchAnalyzer : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }

    void analyze_mismatch(const std::string& preset, int seed) {
        sol_rules rules = rules_parser::from_preset(preset);
        game_state gs(rules, seed, game_state::streamliner_options::NONE);
        dual_cache cache(gs, 10000000);
        solver sol(gs, cache);
        sol.run(boost::optional<std::chrono::milliseconds>(10000));

        if (cache.get_first_mismatch_op() > 0) {
            std::cout << "\n=== MISMATCH: " << preset << " seed " << seed << " ===" << std::endl;
            std::cout << "Op: " << cache.get_first_mismatch_op() << std::endl;
            std::cout << "LRU: " << (cache.get_mismatch_lru_hit() ? "HIT" : "MISS") << std::endl;
            std::cout << "Flat: " << (cache.get_mismatch_flat_hit() ? "HIT" : "MISS") << std::endl;
            std::cout << "Hash: 0x" << std::hex << cache.get_mismatch_zobrist_hash() << std::dec << std::endl;
        } else {
            std::cout << "\n[OK] " << preset << " seed " << seed << " - no mismatches" << std::endl;
        }
    }
};

TEST_F(MismatchAnalyzer, FreeCellSeed1) {
    analyze_mismatch("free-cell", 1);
}

TEST_F(MismatchAnalyzer, BakersGameSeed1) {
    analyze_mismatch("bakers-game", 1);
}

TEST_F(MismatchAnalyzer, SomersetSeed1) {
    analyze_mismatch("somerset", 1);
}

TEST_F(MismatchAnalyzer, FlowerGardenSeed1) {
    analyze_mismatch("flower-garden", 1);
}

TEST_F(MismatchAnalyzer, SeahavenTowersSeed1) {
    analyze_mismatch("seahaven-towers", 1);
}

TEST_F(MismatchAnalyzer, SomersetSeed2) {
    analyze_mismatch("somerset", 2);
}

TEST_F(MismatchAnalyzer, SomersetSeed3) {
    analyze_mismatch("somerset", 3);
}
