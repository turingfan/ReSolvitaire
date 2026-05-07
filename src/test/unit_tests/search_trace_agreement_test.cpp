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
//   no pile canonicalization), so they produce byte-identical event streams up to
//   the timeout boundary.  The comparison stops at the first TIMEOUT event because
//   the two cache implementations run at different wall-clock speeds (16-byte vs
//   64-byte clusters), causing them to hit the solver timeout at different event
//   counts.
//
// HOW FULL COMPARISON WORKS:
//
//   For perfect-agreement games, both caches make identical HIT/MISS decisions at
//   every node.  With a large cap (no evictions), the search trees are identical.
//   The two traces must therefore be byte-identical (event stream only — the header
//   lines DATE and POLICY legitimately differ between runs and are skipped).
//   We compare every event line to EOF and fail if the streams differ in content
//   or length.
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

static const int      HEADER_LINES    = 6;        // TRACE DATE CMD GAME POLICY <blank>
static const char*    k_fake_argv[]   = {"unit_tests"};
static const uint64_t CAP_HASHONLY    = 200000000;// 200M — matches DualCacheAgreementWithFlatOnKlondike
static const int      TIMEOUT_HASHONLY = 5000;    // 5s   — matches DualCacheAgreementWithFlatOnKlondike

// ---------------------------------------------------------------------------
// Streaming full comparison
//
// Opens both trace files, skips HEADER_LINES header lines in each, then reads
// one line at a time from both simultaneously.  Stops and reports on the first
// mismatch or length difference.  Only two lines are in RAM at any moment.
// ---------------------------------------------------------------------------

static bool skip_header(std::ifstream& fh) {
    std::string line;
    for (int i = 0; i < HEADER_LINES; ++i) {
        if (!std::getline(fh, line)) return false;
    }
    return true;
}

void compare_traces_full(const std::string& path_a,
                         const std::string& path_b,
                         const std::string& label) {
    std::ifstream fa(path_a), fb(path_b);
    ASSERT_TRUE(fa.is_open()) << label << ": cannot open trace A: " << path_a;
    ASSERT_TRUE(fb.is_open()) << label << ": cannot open trace B: " << path_b;
    ASSERT_TRUE(skip_header(fa)) << label << ": trace A header too short";
    ASSERT_TRUE(skip_header(fb)) << label << ": trace B header too short";

    std::string line_a, line_b;
    std::size_t event_idx = 0;
    bool        got_event = false;

    while (true) {
        bool ok_a = static_cast<bool>(std::getline(fa, line_a));
        bool ok_b = static_cast<bool>(std::getline(fb, line_b));

        if (!ok_a && !ok_b) break;  // both ended cleanly — success

        if (ok_a != ok_b) {
            ADD_FAILURE() << label << ": traces have different lengths — "
                          << (ok_a ? "A longer than B" : "B longer than A")
                          << " at event index " << event_idx;
            return;
        }

        // Stop cleanly at the timeout boundary in either trace.
        // flat_cache (64-byte clusters) and hash_only_cache (16-byte clusters)
        // run at different wall-clock speeds, so they may hit the solver timeout
        // at different event counts.  Everything before the timeout is comparable;
        // the TIMEOUT event itself is not.
        bool a_timeout = (line_a.find(" TIMEOUT") != std::string::npos);
        bool b_timeout = (line_b.find(" TIMEOUT") != std::string::npos);
        if (a_timeout || b_timeout) break;

        got_event = true;
        if (line_a != line_b) {
            ADD_FAILURE() << label << " diverges at event " << event_idx
                          << "\n  A: " << line_a
                          << "\n  B: " << line_b;
            return;  // stop at first divergence
        }
        ++event_idx;
    }

    EXPECT_TRUE(got_event) << label << ": no events in either trace";
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
// Perfect agreement is expected: same hash → same HIT/MISS at every node.
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

        compare_traces_full(path_a_, path_b_,
                            "klondike-deal-1 hash-only vs flat seed " +
                            std::to_string(seed));
    }
}

}  // namespace

#endif  // SOLVITAIRE_SEARCH_TRACE
