# Development Log: Flat-Cache Refactoring

**Tag baseline:** `pre-refactor-work` (commit `a784c7b`, 2026-04-07)  
**Branch:** `feature/conditional-compilation`, merged to `dev` 2026-04-17  
**Scope:** Phases 0–3 of the architectural refactoring plan described in `execution_strategy.md`

---

## Background

At the `pre-refactor-work` tag, the codebase had three separate but structurally near-identical
flat-cache implementations (`flat_cache`, `hash_only_cache`, `predecessor_flat_cache`), each
hand-coded with the same Fibonacci hashing and TwoBig1 cluster replacement scheme. The Zobrist
hash was maintained via an explicit undo stack (`zobrist_undo_stack`) that stored old descriptor
values before each move. All game states computed the full Zobrist hash and compact-state payload
regardless of which cache was in use at runtime, and there was no mechanism to produce a solver
binary stripped of unused cache code.

The refactoring aimed to: (1) eliminate the undo stack in favour of inline hash reconstruction;
(2) unify the three caches into a single template; (3) produce separate variant binaries with
dead code compiled out for fair benchmarking.

---

## Phase 0: Safety Net and Investigation (2026-04-07 to 2026-04-09)

Before any structural changes, the FreeCell seed-1 investigation clarified a previously observed
anomaly: `flat=HIT, lru=MISS` mismatches from operation 221 onwards were confirmed to be
**legitimate deduplication**, not a false positive. The flat cache correctly identifies duplicate
states that differ only in tableau pile ordering; the LRU cache does not because it encodes pile
order in its hash. This was documented and the investigation committed (`4c4b022`).

The execution strategy and architectural plan were written and committed during this period,
establishing the phase decomposition and the key invariant: the descriptor undo analysis showed
that all old-descriptor values needed for Zobrist undo could be reconstructed from pile state
at undo time, making the explicit undo stack redundant.

---

## Phase 1: Zobrist Inline Computation (2026-04-09 to 2026-04-10)

**Goal:** Remove `zobrist_undo_stack` and `pred_undo_entries`; compute hash deltas directly from
pile state during `undo_move`.

**Key insight:** For each move type, the card's pre-move descriptor can be recovered at undo time
because pile state has already been restored before the hash update fires. For `undo_regular_move`
the old descriptor is `determine_destination_descriptor(m.from, card)` called after pile-undo (the
card is back at `m.from` and the pile below it reflects what was there when the move was first
made). For face-down cards initially dealt face-up, the `initially_face_up[cid]` table (populated
once at construction) determines whether the pre-reveal descriptor was `STARTING` or
`STARTING_FACE_UP`.

**Commits:** Five-commit sequence (Commits B–E) rewrote undo for regular moves, built-group
moves, stock-k-plus moves, and removed the undo stack entirely in Commit E. The intermediate
commits included `VALIDATE_INLINE_UNDO` cross-check scaffolding that compared inline results
against the old stack values before the stack was removed.

**Result:** `zobrist_undo_stack` eliminated. `pred_undo_entries` / `pred_undo_frames` retained
for accordion predecessor moves (accordion was out of scope for this refactor). All existing
regression tests passed.

---

## Phase 2: Template Cache Unification (2026-04-10 to 2026-04-13)

**Goal:** Replace three structurally identical flat-cache implementations with a single
`generic_flat_cache<Policy>` template parameterised on a payload policy struct.

**Design:** Three policy structs in `generic_flat_cache_policies.h`:

| Policy | Cluster size | Key | Use |
|---|---|---|---|
| `CompactStatePolicy` | 64 bytes | hash + full `compact_state` payload | replaces `flat_cache` |
| `HashOnlyPolicy` | 16 bytes | hash only | replaces `hash_only_cache` |
| `PredecessorPolicy` | 128 bytes | hash + `predecessor_state` payload | replaces `predecessor_flat_cache` |

Each policy provides `cluster_count(capacity)`, `matches(cluster, hash, payload)`,
`store(cluster, hash, payload)`, and `is_occupied(cluster)`.

**Commits:**
- **P2-A**: Added `generic_flat_cache.h`, three policy structs, static_asserts on cluster sizes.
- **P2-B**: Unit tests for all three specialisations (`generic_flat_cache_test.cpp`,
  `generic_flat_dual_cache_test.cpp`, `generic_flat_cache_compile_test.cpp`).
- **P2-C**: Wired `cache_factory.h` behind a `USE_GENERIC_CACHE` cmake flag (default OFF) so the
  template could be validated without changing the default binary.
- **P2-D**: `DualCacheTest` parity harness — ran `generic_flat_cache<CompactStatePolicy>` and
  the original `flat_cache` in parallel, asserting identical lookup results across a full solve.
- **P2-E**: Flipped `USE_GENERIC_CACHE` to ON as the default; fixed a `bad_cast` in the solver
  that assumed the concrete `flat_cache` type.

**Result:** The three hand-coded caches are now thin wrappers around (or replaced by)
`generic_flat_cache<Policy>`. The default binary is functionally identical; regression levels
1–4 all pass. `predecessor_flat_cache` retained as a named type for the solver's
`dynamic_cast` (the accordion path checks for it specifically).

---

## Phase 3: Conditional Compilation and Variant Binaries (2026-04-13 to 2026-04-17)

**Goal:** Produce three variant solver binaries — `solvitaire-flat`, `solvitaire-hash-only`,
`solvitaire-lru` — each stripped of code the chosen cache policy does not use.

### Phase 3 structural issues found during implementation

Two bugs were discovered mid-phase when evaluating agent-produced code:

1. **Bug: predecessor routing inverted.** `solvitaire-flat` was refusing accordion games while
   `solvitaire-lru` was silently running them via `lru_cache`. The predecessor flat cache is a
   flat-array cache and belongs in `solvitaire-flat`, not `solvitaire-lru`. Additionally,
   `predecessor_flat_cache.cpp` was compiling unconditionally into all variant binaries,
   polluting `solvitaire-lru`'s symbol table with Zobrist references.

2. **Bug: guards too narrow.** The initial `SOLVITAIRE_COMPUTES_FLAT_HASH` macro only wrapped
   the Zobrist XOR lines. The `payload.set_*()` calls, `compact_state payload` and
   `zobrist_hash_value` members, and the flat-cache `.cpp` files all still compiled into
   `solvitaire-lru`. An important design constraint was identified: because the Zobrist hash
   is computed incrementally using XOR deltas that require the old descriptor value, and old
   descriptors are tracked exclusively inside `compact_state payload`, the `payload` struct must
   be maintained whenever the hash is maintained. This means `computing_flat_hash` (not
   `computing_flat_payload`) is the guard for all `payload.set_*()` calls.

### P3-A: SOLVITAIRE_COMPUTES_FLAT_HASH macro

Added the compile-time flag `SOLVITAIRE_COMPUTES_FLAT_HASH` (1 for all flat/hash-only variants
and the default binary; 0 for `SOLVITAIRE_LRU_ONLY`) and wrapped all Zobrist XOR calls. The
default binary additionally gains two runtime boolean flags (`computing_flat_hash`,
`computing_flat_payload`) set once at construction, enabling dead work to be avoided at runtime
for games routed to LRU (2-deck, spider, suit-symmetry, accordion).

### P3-B and fixes: CMake targets, factory dispatch, flag routing

Added three CMake targets with mutually-exclusive compile definitions. Fixed predecessor routing.
Wrapped `flat_cache.cpp`, `hash_only_cache.cpp`, `compact_state.cpp`, `parent_table.cpp` with
`#if !defined(SOLVITAIRE_LRU_ONLY)` guards. Added runtime boolean guards in the default binary
for the ~40% of game types that route to `lru_cache`.

### P3-C: Graceful error reporting for ineligible flag/game combinations

`solve_input_files()` in `main.cpp` was changed from `void` to `bool` so that ineligible
combinations (e.g. `solvitaire-flat` presented with a 2-deck game) propagate to `EXIT_FAILURE`,
enabling `--skip-ineligible` in the regression runner to fire correctly.

### P3-D: compare_binaries.sh validation harness

A bash harness (`scripts/compare_binaries.sh`) cross-checks that all four binaries agree on
`solution_type` for 10 seeds of klondike and free-cell. Wired into CTest as a smoke-test target.

### P3-E: Regression infrastructure for variant binaries

- `regression_runner.py` extended with `--force-lru`, `--skip-ineligible`, `--compare-outcome-only`,
  `--cache-type` flags.
- 15 new CTest targets: `regression_level{1..5}_{flat,hash_only,lru}`.
- Per-variant hash-only oracles for levels 1–4 generated from the `pre-refactor-work` binary
  (using `--cache-type hash-only`), ensuring non-circular validation.

### P3-E-3: Eliminate determine_destination_descriptor from solvitaire-lru

After verifying via `nm` that `parent_table::get_parents` was still present in `solvitaire-lru`,
all call sites of `determine_destination_descriptor` and `parent_table::get_descriptor_for_parent`
in `game_state.cpp` were guarded with `#if SOLVITAIRE_COMPUTES_FLAT_HASH`. Confirmed absent from
`solvitaire-lru` symbol table after rebuild.

### Regression results at Phase 3 completion

All regression levels 1–3 pass for all four binaries (12/12 CTest targets). Level 4 also passes.

---

## Post-Phase-3: Infrastructure Fixes (2026-04-17)

After merging `feature/conditional-compilation` to `dev`:

**Container builds:** The Dockerfile was updated to build all four variant binaries. The container
test memory limit was raised from 2 GB to 7 GB — the flat cache mmap reservation (100M entries
× 64 bytes = 6.4 GB virtual) caused `std::bad_alloc` at lower limits. This was a pre-existing
known issue (`a5c6b4a`), now resolved in the build script and documented.

**CI (GitHub Actions):** The macOS build was failing because `brew install boost` left boost
installed but unlinked, and newer Boost versions use `BoostConfig.cmake` rather than the legacy
`FindBoost.cmake` module. Fixed by adding `brew link boost` and passing
`CMAKE_PREFIX_PATH=$(brew --prefix boost)` to cmake on macOS.

**Remote benchmark scripts:** `setup_remote.sh` and `collect_results.sh` rewritten to run from
the local machine via SSH, so benchmarking on a remote server requires no manual login. Default
branch changed from `benchmark-python` to `dev`.

**Benchmark orchestrator:** `benchmark_orchestrator.py` extended with `--solver-dir DIR` mode
that discovers all four variant binaries and runs each as a separate labelled configuration.
`run_benchmark.py` extended with `--skip-ineligible` so ineligible game/solver combinations
are skipped gracefully rather than writing KILLED rows.

---

## Known Issues Carried Forward

| # | Summary | Status |
|---|---|---|
| 1 | `json_helper.cpp` uses `gs.tableau_piles` instead of `gs.original_tableau_piles` — breaks JSON round-trips when pile symmetry is active | Open; Levels 2–5 avoid via seed-based runs |
| 2 | Spanish Patience traversal regression from pile-ordering removal in M6 | Accepted; some seeds now OOM/timeout |
| 3 | Flat cache not tested with suit-symmetry streamliner | Accepted; falls back to LRU correctly |
| 8 | Redundant hash/payload computation in default binary for LRU-routed games | Mitigated by runtime boolean guards; long-term fix deferred (templated dispatch) |
| 9 | Descriptor state tracked inside `compact_state payload` rather than a separate array | Design constraint; documented |
| KI-9 | Level 5 hash-only oracle not yet generated | Level 5 still uses `--compare-outcome-only` |

Full details in `docs/known-issues.md`.

---

## Branches and Tags

| Ref | Purpose |
|---|---|
| `pre-refactor-work` | Snapshot of `dev` before any phase work started (2026-04-07) |
| `feature/conditional-compilation` | Phase 3 work branch; merged to `dev` 2026-04-17 |
| `dev` | Current main development branch; all phase work landed here |
| `benchmark-python` | Older benchmarking branch; superseded by `dev` — safe to delete |
