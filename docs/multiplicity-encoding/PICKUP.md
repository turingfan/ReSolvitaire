# PICKUP — multiplicity-encoding branch

**Last updated:** 2026-05-27

## What This Branch Is

Implementing the multiplicity encoding cache system, a new cache policy that enables
suit-symmetry canonicalisation in the flat cache (currently only possible via the slower
LRU cache). The work follows a staged plan with architecture prep first, then from-scratch
implementation, then incremental optimisation.

## Master Plan

`docs/multiplicity-encoding/implementation-plan.md` (this directory)

Stages 0-5.3 complete (including solvability cross-check and trace agreement tests).
Stage 5.4 infrastructure ready. Stage 5.4 execution and Stage 6 (auto-dispatch) remain.

## What's Done

**Scheme A Collapsing Bug Fix** (2026-05-26)
- `incremental_update` Step 0 was using `raw canonical_pos` for predecessor slot computation,
  but `recompute_from_descriptors()` uses `collapsed_pos` (Scheme A). This caused
  incremental/scratch divergence when predecessor targets were in indistinguishable groups.
- Fix: predecessor cards now use `collapsed_pos` in `multiplicity_descriptor_engine.h`
- Regression test: `SchemeACollapsingPredecessorRegression` in `multiplicity_incremental_test.cpp`
- All 3 gates pass on macOS and Linux (verified 2026-05-26).

**KI-21 Waste Descriptor Fix** (2026-05-27, merged from PR #4)
- `MLD_IN_WASTE` collapsed to top-of-waste only; all other waste cards use `MLD_IN_STOCK`
- `stock_k_plus` incremental update: O(1) instead of O(stock_size) — max 4 descriptor changes
- Bugs found in review and fixed: count=0 overwrite, missing hole-top handling
- `trace_mult_vs_flat_klondike` and `trace_mult_vs_flat_canfield` confirmed passing as mult-vs-flat
- 6 new unit tests, all 3 gates pass on macOS (merged result with Scheme A fix)
- `stock_to_all_tableau` remains as `mult_fallback = true` (out of scope)
- Resolves known issue #24

**Stage 5.4 Benchmark Infrastructure** (2026-05-25)
- `scripts/experiments/bench_multiplicity.sh` — experiment orchestrator for 4 comparisons
  (A–D), using `run_benchmark.py` + `xargs -P` for parallel execution with seed chunking
- `solvitaire-mult-scratch` CMake target — compile-time `MULTIPLICITY_NO_INCREMENTAL` flag
  forces `recompute_all()` on every move (Comparison A: incremental vs from-scratch)
- Verified: incremental and from-scratch produce identical `states_searched`
- Benchmark plan: `docs/multiplicity-encoding/stage5-4-benchmark-plan.md`

**Linux Container Build Fix** (2026-05-25)
- GCC `-Wextra` flags enum/non-enum ternary in `MLD_IN_SPACE` expressions — added
  `static_cast<uint8_t>` in `multiplicity_descriptor_engine.h` and test file
- Added `BOOST_BIND_GLOBAL_PLACEHOLDERS` to suppress Boost 1.74 pragma noise
- Documented `container builder delete --force` workaround for BuildKit stale context
  (container CLI v0.9 bug)

**Stage 5.3 — Solvability Cross-Check** (2026-05-24)
- 110 comparisons, 0 mismatches across all game types and symmetry modes
- Results documented in `docs/multiplicity-encoding/stage5-solvability-results.md`

**Multiplicity vs Flat Trace Agreement Tests** (2026-05-24)
- 8 CTest targets (`trace_mult_vs_flat_*`), all pass with `--until-evict`

**STRACE_EVICT Bug Fix** (2026-05-24)
- `generic_flat_cache.h` was missing `STRACE_EVICT()` in `do_replacement` overloads
- Fixed by adding trace events to all 5 eviction paths
- Known issue #25: trace regression reference binaries — now resolved (2026-05-27)

**Stage 4 — Incremental Updates, NONE Mode** (committed: `05f45a8`)
- O(k) fast path via `incremental_update_none()`, 10 unit tests

**Stage 5 — Incremental Updates, With Cascade** (committed: `05f45a8`)
- `incremental_update()` — full BFS cascade for COLOUR/SUIT_IRRELEVANT, 8 tests

**Stages 0-2C** — Architecture prep, from-scratch hash, suit-symmetry canonicalisation,
testing (all committed on earlier commits)

## Stage 5.4 — Preliminary Benchmark Results (2026-05-27)

Benchmarks run on remote Linux server (`benchmarks/mult_20260527_102837/`). Key findings:

**Correctness:**
- **A (incr vs scratch):** All outcomes agree where both completed. Incremental is correct.
- **B (flat vs mult):** All outcomes agree.
- **C (lru vs mult):** C_lru completely broken (100% KILLED, 0 results). No valid comparison.
- **D (lru vs mult, suit-sym):** One disagreement: `klondike-deal-1_527` — mult SOLVED (correct),
  LRU UNWINNABLE. Confirmed pre-existing LRU suit-symmetry false negative (deal is trivially
  solvable in 103 nodes without streamliners). Not a mult bug.

**Performance:**
- **A:** Incremental 2.3x faster than scratch (84.9K vs 37.6K NPS). As expected.
- **B:** Mult 3.1x slower than flat (58.3K vs 183.3K NPS). Expected overhead.
- **C:** Invalid — C_lru produced no results.
- **D (fair comparison, >1s instances where both completed):** 149 instances, mult 486K NPS
  vs LRU 495K NPS — essentially identical per-node throughput (ratio 0.98). Mult's time
  advantage comes from exploring fewer nodes (better suit-symmetry deduplication), not
  faster per-node processing.

**Methodology problems — benchmarking deferred:**
- High KILLED rates distort aggregate NPS (D_lru: 25% KILLED, D_mult: <1%)
- Survivorship bias: KILLED instances are the hardest, dropping them inflates LRU's apparent NPS
- C_lru completely failed (likely OOM or misconfiguration)
- Proper benchmarking needs redesigned experiment with higher resource limits or PAR2-style metrics

## What's Next

**Stage 6** — Auto-dispatch: Flat where possible, Multiplicity where Flat isn't eligible
(including suit-symmetry games), LRU as final fallback. `--force-lru` still works.

**Reference binaries** — Rebuilt on macOS and Linux (known issue #25, resolved 2026-05-27)

## Key Files

| File | Purpose |
|---|---|
| `src/main/game/multiplicity_descriptor.h` | Per-card descriptor: locative(kind, fd) or predecessor(q, fd) |
| `src/main/game/multiplicity_descriptor_store.h` | 64-byte payload store |
| `src/main/game/multiplicity_descriptor_engine.h` | Core: recompute_all(), fixpoint, incremental_update(), cascade |
| `src/main/game/multiplicity_zobrist.h/cpp` | Zobrist table Z[class][column] |
| `src/main/game/multiplicity_static_class.h` | Static class structure for symmetry modes |
| `src/main/game/flat_descriptor_engine.h` | Flat descriptor engine + descriptor_context |
| `src/main/game/generic_flat_cache.h` | Generic flat cache (includes STRACE_EVICT fix) |
| `src/main/game/generic_flat_cache_policies.h` | Cluster policies |
| `src/main/game/cache_policy.h` | Policy structs with engine typedefs |
| `src/main/game/cache_interface.h` | use_multiplicity_cache() eligibility |
| `src/main/game/search-state/game_state.h/cpp` | State class, move logic, mult_desc_at() |
| `src/test/unit_tests/multiplicity_canonicalisation_test.cpp` | Stage 2C tests (13) |
| `src/test/unit_tests/multiplicity_incremental_test.cpp` | Stage 4+5 tests (18) + KI-21 tests (6) + Scheme A regression (1) |
| `scripts/experiments/bench_multiplicity.sh` | Benchmark orchestrator (4 comparisons) |
| `docs/multiplicity-encoding/stage5-solvability-results.md` | Solvability cross-check results |
| `docs/multiplicity-encoding/stage5-4-benchmark-plan.md` | Benchmark plan |

## Design References

| Document | Location |
|---|---|
| v5.1 specification | `01-Knowledge-Base/Design-Documents/multiplicity_encoding_v5.tex` |
| Detailed plan | `docs/multiplicity-encoding/implementation-plan.md` |
| Stage 3 incremental spec | `docs/multiplicity-encoding/stage3-incremental-update-proposal.md` |
| Stage 4-5 implementation plan | `docs/multiplicity-encoding/stage4-5-implementation-plan.md` |
| Stage 2C testing plan | `docs/multiplicity-encoding/stage2c-testing-plan.md` |
| Scheme A bug report | `docs/multiplicity-encoding/bug-scheme-a-hash-collapsing.md` |
| Development roadmap | `01-Knowledge-Base/Implementation-Plans/development-roadmap-2026-05-25.md` |
