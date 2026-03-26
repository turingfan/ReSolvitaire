#include <gtest/gtest.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include "../../main/game/zobrist.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/game/dual_cache.h"
#include "../../main/solver/solver.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"


class FreeCellActualDFS : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }

    void run_actual_dfs() {
        std::ofstream trace_file("/tmp/freecell_actual_dfs_trace.txt");
        std::streambuf* old_clog = std::clog.rdbuf(trace_file.rdbuf());

        sol_rules rules = rules_parser::from_preset("free-cell");
        game_state gs(rules, 1, game_state::streamliner_options::NONE);

        trace_file << "=== FreeCell Seed 1: Actual DFS Trace ===" << std::endl;
        trace_file << "Running solver with DEBUG output (moves, undoes, board states)" << std::endl;
        trace_file << "This shows every board state and move/undo in the DFS exploration" << std::endl;
        trace_file << std::endl;

        dual_cache cache(gs, 10000000);
        solver sol(gs, cache);
        sol.run(boost::optional<std::chrono::milliseconds>(10000));

        std::clog.rdbuf(old_clog);

        trace_file << std::endl << "=== TRACE END ===" << std::endl;
        trace_file << "Total cache operations: " << cache.get_ops() << std::endl;
        if (cache.get_first_mismatch_op() > 0) {
            trace_file << "Mismatch detected at op: " << cache.get_first_mismatch_op() << std::endl;
            trace_file << "  LRU: " << (cache.get_mismatch_lru_hit() ? "HIT" : "MISS") << std::endl;
            trace_file << "  Flat: " << (cache.get_mismatch_flat_hit() ? "HIT" : "MISS") << std::endl;
            trace_file << "  Hash: 0x" << std::hex << cache.get_mismatch_zobrist_hash() << std::dec << std::endl;
        }

        std::cout << "DFS trace saved to /tmp/freecell_actual_dfs_trace.txt" << std::endl;
    }
};

TEST_F(FreeCellActualDFS, RunTrace) {
    run_actual_dfs();
}
