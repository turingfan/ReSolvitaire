# PICKUP — multiplicity-encoding branch

**Last updated:** 2026-05-26

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

**KI-21 Investigation** (2026-05-26)
- KI-21 (waste O(stock) descriptor updates) investigated on Claude web
- Implementation plan and prompts committed: `docs/multiplicity-encoding/ki21-*.md`
- This is independent work from the Scheme A fix above

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
- Known issue #22: trace regression reference binaries need rebuild

**Stage 4 — Incremental Updates, NONE Mode** (committed: `05f45a8`)
- O(k) fast path via `incremental_update_none()`, 10 unit tests

**Stage 5 — Incremental Updates, With Cascade** (committed: `05f45a8`)
- `incremental_update()` — full BFS cascade for COLOUR/SUIT_IRRELEVANT, 8 tests

**Stages 0-2C** — Architecture prep, from-scratch hash, suit-symmetry canonicalisation,
testing (all committed on earlier commits)

## What's Next

**Stage 5.4** — Run benchmarks on remote machine (infrastructure ready)

**Stage 6** — Auto-dispatch (multiplicity cache becomes default for eligible games)

**Reference binaries** — Need rebuild after STRACE_EVICT fix is on dev (known issue #22)

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
| `src/test/unit_tests/multiplicity_incremental_test.cpp` | Stage 4+5 tests (18) |
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
