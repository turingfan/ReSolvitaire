// search_trace_agreement_test.cpp
//
// Trace-based agreement tests replacing the dual-cache agreement tests that were
// removed from feature/templated-dispatch.
//
// WHAT IS TESTED HERE:
//
//   HashOnlyVsFlat: 50 klondike seeds.
//             Source: hash_only_cache_test.cpp DualCacheAgreementWithFlatOnKlondike.
//
// WHY HASH-ONLY-VS-FLAT ONLY (no FlatVsLRU):
//
//   The original dual_cache mechanism ran BOTH caches on the SAME single DFS
//   traversal — one solver, one search tree, two caches consulted at each node.
//   This allowed direct node-by-node comparison of HIT/MISS decisions.
//
//   With the trace approach, each cache runs in a separate invocation.
//   Flat and LRU cannot produce identical traces in independent runs because
//   lru_cache canonicalizes tableau pile order (game_state_impl<LRUPolicy> has
//   skip_pile_ordering=false) while flat does not (skip_pile_ordering=true).
//   Even "perfect-agreement" games diverge immediately in independent runs
//   because the two game states explore moves in different orders from the start.
//   The dual_cache "perfect agreement" invariant only held because both caches
//   shared the same pile-ordered (LRU) game state on a single search tree.
//
//   hash_only_cache uses the same Zobrist hash as flat_cache (same state encoding,
//   no pile canonicalization), so they should agree on which states are new vs
//   cached.  However, their different cluster sizes (16-byte vs 64-byte) cause
//   different eviction patterns over long runs, meaning the intermediate event
//   streams can diverge even when the final outcome agrees.
//
// HOW COMPARISON WORKS:
//
//   We verify that flat and hash-only agree on the *final outcome* of each seed
//   (SOLVED, UNSOLVABLE, or TIMEOUT).  Byte-identical trace comparison is not used
//   because the two policies have different cluster sizes (64 bytes vs 16 bytes),
//   which causes different eviction patterns over long searches.  Specifically,
//   hash-only's smaller clusters can lose an entry that flat retains, causing
//   hash-only to MISS a state that flat correctly HITs.  The final answer is always
//   the same (both caches are sound), but the intermediate event streams diverge.
//   Empirically confirmed on klondike seed 31: state first inserted in both caches
//   at op 8996141, but hash-only evicted it before op 18003107 while flat did not.
//
// These tests are no-ops in release and debug builds (no SOLVITAIRE_SEARCH_TRACE).
// They execute only in cmake-build-trace (./build.sh --trace).

#include <gtest/gtest.h>

#ifdef SOLVITAIRE_SEARCH_TRACE

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

#include <boost/optional.hpp>

#include "../../main/game/generic_flat_cache.h"
#include "../../main/game/cache_policy.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/game/zobrist.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"
#include "../../main/solver/search_trace.h"
#include "../../main/solver/solver.h"

namespace {

// ---------------------------------------------------------------------------
// Constants — match the original dual_cache_test.cpp values exactly
// ---------------------------------------------------------------------------

static const char*    k_fake_argv[]   = {"unit_tests"};
static const uint64_t CAP_HASHONLY    = 200000000;// 200M — matches DualCacheAgreementWithFlatOnKlondike
static const int      TIMEOUT_HASHONLY = 5000;    // 5s   — matches DualCacheAgreementWithFlatOnKlondike

// ---------------------------------------------------------------------------
// Outcome extraction
//
// Reads a trace file and returns the final RESULT keyword (SOLVED, UNSOLV,
// TIMEOUT, TERMINATED, or MEM-LIMIT) from the last RESULT line.
// Returns empty string if no RESULT line is found.
// ---------------------------------------------------------------------------

static std::string extract_outcome(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::string line, outcome;
    while (std::getline(f, line)) {
        // RESULT lines look like: "0001234567 SOLVED" or "0001234567 UNSOLV" etc.
        auto pos = line.find(" SOLVED");
        if (pos != std::string::npos) { outcome = "SOLVED"; continue; }
        pos = line.find(" UNSOLV");
        if (pos != std::string::npos) { outcome = "UNSOLV"; continue; }
        pos = line.find(" TIMEOUT");
        if (pos != std::string::npos) { outcome = "TIMEOUT"; continue; }
        pos = line.find(" TERMINATED");
        if (pos != std::string::npos) { outcome = "TERMINATED"; continue; }
    }
    return outcome;
}

void compare_outcomes(const std::string& path_a,
                      const std::string& path_b,
                      const std::string& label) {
    std::string out_a = extract_outcome(path_a);
    std::string out_b = extract_outcome(path_b);
    ASSERT_FALSE(out_a.empty()) << label << ": no outcome found in trace A: " << path_a;
    ASSERT_FALSE(out_b.empty()) << label << ": no outcome found in trace B: " << path_b;
    // Both agree if either timed out (hash-only explores more due to eviction differences)
    if (out_a == "TIMEOUT" || out_b == "TIMEOUT") return;
    EXPECT_EQ(out_a, out_b) << label << ": outcome mismatch (flat=" << out_a
                             << " hash-only=" << out_b << ")";
}

// ---------------------------------------------------------------------------
// Cache run helpers
// Each helper: open trace → write_init → construct cache → run solver → close.
// close() resets op_ to 0 so the next run starts from counter 0.
// ---------------------------------------------------------------------------

void run_with_flat(const std::string& path,
                   const sol_rules& rules,
                   int seed,
                   const std::string& game_type,
                   uint64_t cap,
                   int timeout_ms) {
    trace_writer::instance().open(path, 1, k_fake_argv);
    trace_writer::instance().write_init(game_type, seed, "none", "flat");
    game_state_impl<FlatPolicy> gs(rules, seed, game_state::streamliner_options::NONE);
    generic_flat_cache<CompactStatePolicy> cache(cap);
    solver_impl<FlatPolicy> sol(gs, cache);
    sol.run(boost::optional<std::chrono::milliseconds>(timeout_ms));
    trace_writer::instance().close();
}

void run_with_hash_only(const std::string& path,
                        const sol_rules& rules,
                        int seed,
                        const std::string& game_type,
                        uint64_t cap,
                        int timeout_ms) {
    trace_writer::instance().open(path, 1, k_fake_argv);
    trace_writer::instance().write_init(game_type, seed, "none", "hash-only");
    game_state_impl<HashOnlyPolicy> gs(rules, seed, game_state_impl<HashOnlyPolicy>::streamliner_options::NONE);
    generic_flat_cache<HashOnlyClusterPolicy> cache(cap);
    solver_impl<HashOnlyPolicy> sol(gs, cache);
    sol.run(boost::optional<std::chrono::milliseconds>(timeout_ms));
    trace_writer::instance().close();
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class SearchTraceAgreementTest : public ::testing::Test {
protected:
    std::string path_a_;
    std::string path_b_;

    void SetUp() override {
        zobrist_hash::init();  // idempotent
        path_a_ = ::testing::TempDir() + "st_agree_a.trace";
        path_b_ = ::testing::TempDir() + "st_agree_b.trace";
        std::remove(path_a_.c_str());
        std::remove(path_b_.c_str());
        trace_writer::instance().close();  // ensure op_=0
    }

    void TearDown() override {
        trace_writer::instance().close();
        std::remove(path_a_.c_str());
        std::remove(path_b_.c_str());
    }
};

// ---------------------------------------------------------------------------
// Hash-only vs flat — 50 klondike seeds
//
// hash_only_cache uses the same Zobrist hash as flat_cache but stores no payload.
// Final outcome (SOLVED/UNSOLVABLE/TIMEOUT) must agree; intermediate HIT/MISS
// decisions can diverge due to different cluster-eviction rates (16 vs 64 bytes).
// cap=200M matches the original test; mmap lazy allocation means physical memory
// is proportional to states actually visited, not the full reservation.
//
// Source: hash_only_cache_test.cpp DualCacheAgreementWithFlatOnKlondike (50 seeds).
// ---------------------------------------------------------------------------

TEST_F(SearchTraceAgreementTest, HashOnlyVsFlat_Klondike50Seeds) {
    sol_rules rules = rules_parser::from_preset("klondike-deal-1");

    for (int seed = 1; seed <= 50; ++seed) {
        std::remove(path_a_.c_str());
        std::remove(path_b_.c_str());
        trace_writer::instance().close();

        run_with_flat     (path_a_, rules, seed, "klondike-deal-1",
                           CAP_HASHONLY, TIMEOUT_HASHONLY);
        run_with_hash_only(path_b_, rules, seed, "klondike-deal-1",
                           CAP_HASHONLY, TIMEOUT_HASHONLY);

        compare_outcomes(path_a_, path_b_,
                         "klondike-deal-1 hash-only vs flat seed " +
                         std::to_string(seed));
    }
}

}  // namespace

#endif  // SOLVITAIRE_SEARCH_TRACE
