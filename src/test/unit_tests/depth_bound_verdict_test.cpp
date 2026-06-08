// depth_bound_verdict_test.cpp
//
// Part A of the Stage-2 item-2d adversarial test suite (see plan §4.2/§4.3): the
// ENGINE-LEVEL soundness guard.
//
// It runs the ACTUAL solver on cycle/transposition-stressing inputs under a
// bounded iterative-deepening configuration (small initial depth bound, geometric
// growth) and asserts that the FINAL winnable/unwinnable verdict equals the
// UNBOUNDED (ground-truth) verdict for every case.  This is the project's red line
// stated as a unit test (proposal §7.2; plan §4.2):
//
//     "the bounded/ID solver's final winnable/unwinnable verdict must equal the
//      unbounded verdict — any single disagreement is a hard failure."
//
// These tests PASS on today's (sound) engine and will FAIL if Stage-2 item 2b ever
// introduces a wrong verdict on these inputs.
//
// ─── Why these inputs (GHI / cycle / transposition stress) ───────────────────
//   * Free-cell, klondike, canfield, fortunes-favor, somerset, spanish-patience:
//     cells / reserve / stock give reversible moves => the state graph contains
//     genuine CYCLES (proposal §3.5: "Solvitaire graphs do contain cycles"; the
//     engine's creates_immediate_loop() is essentially a no-op, so cycle/ancestor
//     handling is done entirely by the cache).  Cross-pass reuse over cyclic graphs
//     is exactly where GHI bites.
//   * Baker's-dozen, alpha-star, gaps: produce many UNWINNABLE instances at modest
//     depth — the verdict 2b is most dangerous for (a wrong cycle/reuse rule yields
//     a FALSE unwinnable).
//   * Black-hole: a DAG (no cycles) but a heavily TRANSPOSING space (many move
//     orders reach the same set) => stresses the transposition-reuse path.
// The small-deck "-test-*" rule variants (and the small gaps one-deal) are used so
// each instance resolves to a definite verdict within a few milliseconds while
// still forcing SEVERAL bounded passes (truncate -> deepen), so the
// truncation/deepening machinery is exercised.
//
// ─── What "iterative deepening" means here, and the 2b switch ────────────────
// The drivers below mirror the engine's Stage-1 outer loop (main.cpp
// solve_game_impl): run solver_impl::run(timeout, L) for L = L0, L0*grow, ...,
// taking the verdict when it becomes SOLVED/UNSOLVABLE.
//
//   * FRESH cache per pass (id_*_fresh) — the SOUND configuration on TODAY's engine
//     (Stage 1) and what the always-on tests assert.  When 2b lands, the verdict it
//     must continue to produce on these inputs is precisely the unbounded verdict
//     these tests pin.
//   * REUSED cache across passes (id_flat_reuse) — the Stage-2 (2b) configuration.
//     On TODAY's Stage-1 engine it is KNOWN-UNSOUND (a node cached in a shallow
//     truncated pass is pruned forever by the plain "in cache => backtrack" rule =>
//     FALSE unwinnable), so it is NOT asserted here; it lives in a DISABLED_ test
//     that documents the contract 2b must satisfy.  After 2b implements
//     DEAD/OPEN(b)/g_min reuse, the orchestrator enables that test (rename without
//     DISABLED_) and it must then match the unbounded verdict for every case — at
//     which point Part A directly guards the cross-pass GHI behaviour on the live
//     engine.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <boost/optional.hpp>

#include "../../main/game/cache_policy.h"
#include "../../main/game/generic_flat_cache.h"
#include "../../main/game/generic_flat_cache_policies.h"
#include "../../main/game/global_cache.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/game/zobrist.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"
#include "../../main/solver/solver.h"

namespace {

using sot = solver_result::type;

// Per-instance time budget. Generous relative to the few-ms these small-deck
// instances actually take, so a slow CI box never turns a sound verdict into a
// spurious TIMEOUT mismatch.
constexpr int    kTimeoutMs   = 8000;
constexpr uint64_t kCacheCap  = 1u << 22;   // 4 Mi entries; mmap-lazy, cheap.

// Iterative-deepening schedule. A small L0 forces several truncate->deepen passes
// (so the bounded machinery is genuinely exercised) while these instances still
// converge fast.
constexpr uint64_t kL0    = 4;
constexpr uint64_t kGrow  = 2;
constexpr uint64_t kLmax  = 1u << 20;       // ample for max depths seen here (<100)

const char* verdict_name(sot t) {
    switch (t) {
        case sot::SOLVED:            return "SOLVED";
        case sot::UNSOLVABLE:        return "UNSOLVABLE";
        case sot::BOUNDED_EXHAUSTED: return "BOUNDED_EXHAUSTED";
        case sot::TIMEOUT:           return "TIMEOUT";
        case sot::MEM_LIMIT:         return "MEM_LIMIT";
        case sot::TERMINATED:        return "TERMINATED";
    }
    return "?";
}

bool is_definitive(sot t) { return t == sot::SOLVED || t == sot::UNSOLVABLE; }

// ─── Policy-generic helpers for the flat-cache families (movable cache) ───────
// Used for FlatPolicy / HashOnlyPolicy / PredecessorPolicy / MultiplicityPolicy.

template <typename Policy>
sot unbounded_flat(const sol_rules& rules, int seed) {
    game_state_impl<Policy> gs(rules, seed, game_state_impl<Policy>::streamliner_options::NONE);
    typename Policy::cache_type cache(kCacheCap);
    solver_impl<Policy> sol(gs, cache);
    return sol.run(std::chrono::milliseconds(kTimeoutMs)).sol_type;
}

// Iterative deepening with a FRESH cache per pass (the sound Stage-1 config).
template <typename Policy>
sot id_flat_fresh(const sol_rules& rules, int seed) {
    uint64_t L = kL0;
    sot last = sot::BOUNDED_EXHAUSTED;
    for (;;) {
        game_state_impl<Policy> gs(rules, seed, game_state_impl<Policy>::streamliner_options::NONE);
        typename Policy::cache_type cache(kCacheCap);     // fresh each pass
        solver_impl<Policy> sol(gs, cache);
        last = sol.run(std::chrono::milliseconds(kTimeoutMs), boost::optional<uint64_t>(L)).sol_type;
        if (is_definitive(last) || last == sot::TIMEOUT || last == sot::MEM_LIMIT)
            return last;
        if (L >= kLmax) return sot::TIMEOUT;
        uint64_t next = L * kGrow;
        if (next <= L) next = L + 1;                       // non-progress guard
        L = next;
    }
}

// Iterative deepening REUSING one cache across passes (the Stage-2 / 2b config).
// KNOWN-UNSOUND on today's engine (see file header) — used only by the DISABLED_
// documentation test.
template <typename Policy>
sot id_flat_reuse(const sol_rules& rules, int seed) {
    uint64_t L = kL0;
    sot last = sot::BOUNDED_EXHAUSTED;
    typename Policy::cache_type cache(kCacheCap);          // shared across passes
    for (;;) {
        game_state_impl<Policy> gs(rules, seed, game_state_impl<Policy>::streamliner_options::NONE);
        solver_impl<Policy> sol(gs, cache);
        last = sol.run(std::chrono::milliseconds(kTimeoutMs), boost::optional<uint64_t>(L)).sol_type;
        if (is_definitive(last) || last == sot::TIMEOUT || last == sot::MEM_LIMIT)
            return last;
        if (L >= kLmax) return sot::TIMEOUT;
        uint64_t next = L * kGrow;
        if (next <= L) next = L + 1;
        L = next;
    }
}

// ─── LRU specialisations (lru_cache needs the game state and is non-movable) ──

sot unbounded_lru(const sol_rules& rules, int seed) {
    game_state_impl<LRUPolicy> gs(rules, seed, game_state_impl<LRUPolicy>::streamliner_options::NONE);
    lru_cache cache(gs, kCacheCap);
    solver_impl<LRUPolicy> sol(gs, cache);
    return sol.run(std::chrono::milliseconds(kTimeoutMs)).sol_type;
}

sot id_lru_fresh(const sol_rules& rules, int seed) {
    uint64_t L = kL0;
    sot last = sot::BOUNDED_EXHAUSTED;
    for (;;) {
        game_state_impl<LRUPolicy> gs(rules, seed, game_state_impl<LRUPolicy>::streamliner_options::NONE);
        lru_cache cache(gs, kCacheCap);                    // fresh each pass (in place)
        solver_impl<LRUPolicy> sol(gs, cache);
        last = sol.run(std::chrono::milliseconds(kTimeoutMs), boost::optional<uint64_t>(L)).sol_type;
        if (is_definitive(last) || last == sot::TIMEOUT || last == sot::MEM_LIMIT)
            return last;
        if (L >= kLmax) return sot::TIMEOUT;
        uint64_t next = L * kGrow;
        if (next <= L) next = L + 1;
        L = next;
    }
}

// Iterative deepening REUSING one LRU cache across passes — the Stage-2 (2b)
// configuration on the IMPLEMENTED path (decision Q6 / B4: LRU first). This mirrors
// exactly what the product's solve_game_impl now does for LRUPolicy: a single
// persistent cache, fresh game state per pass, growing L. On TODAY's engine the
// DFSTT3 DEAD/OPEN(b)/g_min reuse + cycle backup make this match the unbounded
// verdict; a naive cross-pass prune would FALSELY report unwinnable (the bug the
// teeth test below catches).
sot id_lru_reuse(const sol_rules& rules, int seed) {
    uint64_t L = kL0;
    sot last = sot::BOUNDED_EXHAUSTED;
    game_state_impl<LRUPolicy> seed_gs(rules, seed, game_state_impl<LRUPolicy>::streamliner_options::NONE);
    lru_cache cache(seed_gs, kCacheCap);                    // shared across passes
    for (;;) {
        game_state_impl<LRUPolicy> gs(rules, seed, game_state_impl<LRUPolicy>::streamliner_options::NONE);
        solver_impl<LRUPolicy> sol(gs, cache);
        last = sol.run(std::chrono::milliseconds(kTimeoutMs), boost::optional<uint64_t>(L)).sol_type;
        if (is_definitive(last) || last == sot::TIMEOUT || last == sot::MEM_LIMIT)
            return last;
        if (L >= kLmax) return sot::TIMEOUT;
        uint64_t next = L * kGrow;
        if (next <= L) next = L + 1;
        L = next;
    }
}

// ─── The core check: ID (fresh-cache) verdict == unbounded verdict ────────────
// Returns a per-case outcome. A non-definitive ground truth, or an ID run that
// does not converge within the (generous) budget, is NOT a soundness failure —
// only a MISMATCH between two DEFINITIVE verdicts is. We tally outcomes across the
// battery and fail the test only on a mismatch (and require that *some* cases
// actually matched, so a silently-all-skipped run can't pass vacuously).
//
// IMPORTANT: we deliberately do NOT call GTEST_SKIP() per case — in a loop it would
// abort the entire battery on the first slow/non-definitive case (e.g. in the much
// slower debug build), losing coverage of every later case. We record skips and
// keep going instead.

enum class CaseOutcome { MATCHED, MISMATCHED, SKIPPED };

struct Tally {
    int matched = 0, mismatched = 0, skipped = 0;
    void add(CaseOutcome o) {
        if (o == CaseOutcome::MATCHED)      ++matched;
        else if (o == CaseOutcome::MISMATCHED) ++mismatched;
        else                                ++skipped;
    }
};

// Runs one case and ADD_FAILUREs on a definitive mismatch (the red line). Returns
// the outcome so the caller can tally and require non-vacuous coverage.
template <typename Unbounded, typename Id>
CaseOutcome check_case(Unbounded&& run_unbounded, Id&& run_id,
                       int seed, const std::string& label) {
    const sot truth = run_unbounded();
    if (!is_definitive(truth)) return CaseOutcome::SKIPPED;     // no usable oracle
    const sot id = run_id();
    if (id == sot::TIMEOUT || id == sot::MEM_LIMIT) return CaseOutcome::SKIPPED;  // slow box
    if (id != truth) {
        ADD_FAILURE()
            << label << " seed " << seed
            << ": iterative-deepening final verdict " << verdict_name(id)
            << " != unbounded verdict " << verdict_name(truth)
            << "  [RED LINE: a depth-bounded final verdict MUST equal the "
               "unbounded verdict]";
        return CaseOutcome::MISMATCHED;
    }
    return CaseOutcome::MATCHED;
}

// A small adversarial battery: (preset, seed-range). Seeds chosen from probing so
// each is definitive and fast yet forces multiple bounded passes; the ranges mix
// solvable and unsolvable instances.
struct Battery { const char* preset; int seed_lo; int seed_hi; };

// FLAT-cache games (default policy for single-deck no-symmetry games). All are
// small-deck "-test-*" variants (or the small gaps one-deal) so every instance
// resolves definitively in milliseconds even in the debug build, while still
// forcing several truncate->deepen passes.
const Battery kFlatBatteries[] = {
    {"-test-free-cell",        1, 10},  // cells -> cycles; s1 UNSOLVABLE, rest SOLVED
    {"-test-bakers-dozen",     1, 10},  // mostly UNSOLVABLE at depth 5-10 (red-line)
    {"-test-klondike",         1, 10},  // stock cycling -> transpositions; mixed
    {"-test-canfield",         1, 10},  // reserve + stock -> transpositions
    {"-test-fortunes-favor",   1, 10},  // mixed solvable/unsolvable
    {"-test-somerset",         1, 10},  // mixed
    {"-test-spanish-patience", 1, 10},  // mixed
    {"-test-alpha-star",       1, 10},  // many shallow UNSOLVABLE
    {"-test-black-hole",       1, 10},  // transposing DAG; mixed (max depth ~19)
    {"gaps-one-deal",          1,  8},  // explicit gaps cycles; UNSOLVABLE
};

// LRU is the FIRST target for Stage 2 (decision Q6), so we guard it explicitly.
// Only fast small-deck games (LRU canonicalisation is slower than flat).
const Battery kLruBatteries[] = {
    {"-test-free-cell",    1, 6},
    {"-test-bakers-dozen", 1, 6},
    {"-test-klondike",     1, 6},
    {"-test-canfield",     1, 6},
};

}  // namespace

// ─── Fixture ──────────────────────────────────────────────────────────────────

class DepthBoundVerdictTest : public ::testing::Test {
protected:
    void SetUp() override { zobrist_hash::init(); }  // idempotent
};

// ─── ALWAYS-ON guards: fresh-cache iterative deepening == unbounded verdict ────

TEST_F(DepthBoundVerdictTest, FlatIdMatchesUnbounded_AdversarialBattery) {
    Tally t;
    for (const Battery& b : kFlatBatteries) {
        const sol_rules rules = rules_parser::from_preset(b.preset);
        for (int seed = b.seed_lo; seed <= b.seed_hi; ++seed) {
            t.add(check_case(
                [&] { return unbounded_flat<FlatPolicy>(rules, seed); },
                [&] { return id_flat_fresh<FlatPolicy>(rules, seed); },
                seed, b.preset));
        }
    }
    // No mismatch is the red line; require that a healthy majority actually matched
    // (so a machine that skips everything cannot pass vacuously).
    EXPECT_EQ(t.mismatched, 0);
    EXPECT_GT(t.matched, 30) << "too few definitive matches (matched=" << t.matched
                             << " skipped=" << t.skipped << ")";
    RecordProperty("flat_matched", t.matched);
    RecordProperty("flat_skipped", t.skipped);
}

TEST_F(DepthBoundVerdictTest, LruIdMatchesUnbounded_AdversarialBattery) {
    Tally t;
    for (const Battery& b : kLruBatteries) {
        const sol_rules rules = rules_parser::from_preset(b.preset);
        for (int seed = b.seed_lo; seed <= b.seed_hi; ++seed) {
            t.add(check_case(
                [&] { return unbounded_lru(rules, seed); },
                [&] { return id_lru_fresh(rules, seed); },
                seed, std::string(b.preset) + " (LRU)"));
        }
    }
    EXPECT_EQ(t.mismatched, 0);
    EXPECT_GT(t.matched, 10) << "too few definitive LRU matches (matched=" << t.matched
                             << " skipped=" << t.skipped << ")";
    RecordProperty("lru_matched", t.matched);
    RecordProperty("lru_skipped", t.skipped);
}

// ─── Stage-2b TEETH (LRU): cross-pass cache REUSE == unbounded verdict ─────────
//
// THE soundness gate for the implemented 2b path. Unlike the fresh-cache test
// above, this REUSES one LRU cache across all iterative-deepening passes — exactly
// the product configuration (solve_game_impl, LRUPolicy) and the regime where GHI /
// cross-pass-reuse hazards bite (a node cached in a shallow truncated pass must NOT
// be pruned forever). On today's DFSTT3 engine it MUST match the unbounded verdict
// for every definitive case. It has TEETH: a naive cross-pass prune (DEAD/OPEN-blind)
// reproduces the false-`unwinnable` failures (verified during 2b sign-off by
// temporarily disabling the reuse discipline — the same battery then mismatches, as
// the flat DISABLED_ test below still demonstrates on the un-fixed flat path).
TEST_F(DepthBoundVerdictTest, LruReuseAcrossPasses_MatchesUnbounded) {
    Tally t;
    for (const Battery& b : kLruBatteries) {
        const sol_rules rules = rules_parser::from_preset(b.preset);
        for (int seed = b.seed_lo; seed <= b.seed_hi; ++seed) {
            t.add(check_case(
                [&] { return unbounded_lru(rules, seed); },
                [&] { return id_lru_reuse(rules, seed); },
                seed, std::string(b.preset) + " (LRU reuse)"));
        }
    }
    EXPECT_EQ(t.mismatched, 0)
        << "Stage-2b RED LINE: cross-pass LRU reuse produced a verdict != unbounded";
    EXPECT_GT(t.matched, 10) << "too few definitive LRU-reuse matches (matched=" << t.matched
                             << " skipped=" << t.skipped << ")";
    RecordProperty("lru_reuse_matched", t.matched);
    RecordProperty("lru_reuse_skipped", t.skipped);
}

// A focused, low-noise sentinel on the single most important property: a
// depth-bounded run must NEVER turn a winnable instance into `unwinnable` (the
// false-unwinnable red line) on a seed known to be solvable, nor an unwinnable one
// into a (false) win.
TEST_F(DepthBoundVerdictTest, NeverFlipsVerdictOnKnownInstances) {
    zobrist_hash::init();
    // Known SOLVABLE (probed): -test-free-cell seed 2 (max depth ~27).
    {
        const sol_rules r = rules_parser::from_preset("-test-free-cell");
        ASSERT_EQ(unbounded_flat<FlatPolicy>(r, 2), sot::SOLVED);
        EXPECT_EQ(id_flat_fresh<FlatPolicy>(r, 2), sot::SOLVED)
            << "depth-bounded ID must not lose a known win (false unwinnable risk)";
    }
    // Known UNSOLVABLE (probed): -test-bakers-dozen seed 1 (max depth ~8).
    {
        const sol_rules r = rules_parser::from_preset("-test-bakers-dozen");
        ASSERT_EQ(unbounded_flat<FlatPolicy>(r, 1), sot::UNSOLVABLE);
        EXPECT_EQ(id_flat_fresh<FlatPolicy>(r, 1), sot::UNSOLVABLE)
            << "depth-bounded ID must not fabricate a win on an unwinnable instance";
    }
}

// ─── Stage-2 (2b) FLAT cross-pass reuse — DISABLED: flat 2b is DEFERRED ───────
//
// The SAME adversarial battery driven with a FLAT cache REUSED ACROSS PASSES. Stage
// 2b landed for LRU only (decision Q6 / B4: "LRU first"); the flat cache still has
// no update-on-hit path or live bit (proposal §6.4), so its cross-pass reuse is the
// naive "in cache => prune" rule, which is KNOWN-UNSOUND here (a shallow-pass entry
// is pruned forever => false `unwinnable`). The product therefore keeps a FRESH
// cache per pass for flat games (sound Stage-1 behaviour); this test stays DISABLED_
// and serves two purposes: (1) it documents the contract a future FLAT 2b must
// satisfy (enable it then), and (2) force-run today it demonstrates the false-
// `unwinnable` failures, proving the LRU teeth test above (LruReuseAcrossPasses_…)
// guards a real, reproducible hazard class.
TEST_F(DepthBoundVerdictTest, DISABLED_Stage2_FlatReuseAcrossPasses_DeferredFlat2b) {
    int checked = 0, mismatches = 0;
    for (const Battery& b : kFlatBatteries) {
        const sol_rules rules = rules_parser::from_preset(b.preset);
        for (int seed = b.seed_lo; seed <= b.seed_hi; ++seed) {
            const sot truth = unbounded_flat<FlatPolicy>(rules, seed);
            if (!is_definitive(truth)) continue;
            const sot id = id_flat_reuse<FlatPolicy>(rules, seed);
            if (id == sot::TIMEOUT || id == sot::MEM_LIMIT) continue;
            ++checked;
            if (id != truth) {
                ++mismatches;
                ADD_FAILURE()
                    << b.preset << " seed " << seed
                    << ": cross-pass-reuse ID verdict " << verdict_name(id)
                    << " != unbounded " << verdict_name(truth)
                    << "  [Stage-2 RED LINE]";
            }
        }
    }
    EXPECT_GT(checked, 0);
    EXPECT_EQ(mismatches, 0);
}
