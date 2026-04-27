# Known Issues

This file tracks open issues in the `refactor-caching` branch. Resolved issues that
are interesting as development history are documented in `docs/resolved-bugs/`.

---

## Open Issues

### 1. JSON Deal Round-Trip Changes Node Counts (`json_helper.cpp`)

**Affected file:** `src/main/input-output/input/json-parsing/json_helper.cpp`
**Status:** Open in `refactor-caching`; fixed in `claude/quizzical-darwin`
**Impact:** Different `states_searched` counts when running from exported JSON vs seed

`json_helper::print_game_state_as_json` serialises tableau piles by iterating
`gs.tableau_piles` (the runtime-reordered list) rather than `gs.original_tableau_piles`
(the fixed construction order). When pile symmetry has reordered the piles, the
serialised JSON records them in a different order than the parser expects, producing
a logically identical but internally different game state.

**Workaround:** The Level 2–5 regression runner invokes the solver with `--random <seed>`
directly, bypassing JSON serialisation. Level 1 is unaffected in practice.

**Fix (one line, in `claude/quizzical-darwin`):**
```cpp
// Change in json_helper::print_game_state_as_json:
for (auto pr : gs.original_tableau_piles)  // was: gs.tableau_piles
```

### 2. Spanish Patience Traversal Regression (pile ordering)

**Affected game type:** `spanish-patience` (13 tableau piles, any-suit build)
**Status:** Open; accepted for first delivery
**Impact:** Some solvable instances OOM or timeout; oracle entries marked as `timeout`

With pile ordering removed (M6), the DFS traversal order for Spanish Patience degrades
significantly for some seeds. The pile ordering previously served a dual purpose:
deduplication (now handled by the descriptor hash) and implicit move ordering (now lost).
For games with many tableau piles, the move ordering effect can be large.

The regression suite treats these as soft passes (TIMEOUT is acceptable). Correctness
is not affected — unsolvable games are still proven unsolvable. Some formerly-solvable
seeds now time out or OOM at any practically-runnable timeout.

**Affected oracle entries (marked `solution_type: timeout`):**
- Level 5: `spanish-patience_2921115_winnable.json` — formerly solved in 115s with 15M
  nodes; now OOM-killed under 30-minute regeneration timeout.

**Possible future fix:** A lightweight move-ordering heuristic that does not require
full pile sorting. Deferred post-merge.

### 3. Flat Cache Not Tested With Suit-Symmetry Streamliner

**Affected game types:** any game run with `--streamliners suit-symmetry` or `--streamliners both`
(e.g. Spanish Patience, Klondike with suit-symmetry)
**Status:** Open; flat cache correctly falls back to LRU, but flat cache path is untested
**Impact:** Regression tests and benchmarks do not exercise the flat cache for suit-symmetry games

**Root cause:** The flat cache hashes on actual card identity (`Z_card[52][16]`), so two
suit-symmetric board states produce different hashes. The LRU cache with pile ordering
canonicalises these as identical, giving deduplication across suit-symmetric branches.
Running the flat cache with suit-symmetry would still give correct outcomes, but at much
higher node counts (no suit-symmetric deduplication in the cache), causing timeouts on
instances that are fast with LRU.

**Current mitigation (`cache_interface.h`):** `use_new_cache()` returns `false` when
suit-symmetry is active, so the solver automatically falls back to LRU. This restores
correct and efficient behaviour for those games.

**Remaining gaps:**
- The flat cache code path is never exercised by any regression oracle instance that
  uses `streamliner: both` or `streamliner: suit-symmetry`. Any regression or correctness
  testing for suit-symmetry games tests the LRU cache only.
- Benchmark results for suit-symmetry games (e.g. Spanish Patience) reflect LRU
  performance, not flat cache performance.
- A future implementation of suit-canonical hashing in the flat cache (normalising suit
  assignments before computing the Zobrist hash) would enable flat cache use with
  suit-symmetry and should be validated on this game type.

**Where to address:** benchmarking branch / post-merge enhancement.

### 4. Benchmarks Do Not Cover Suit-Symmetry / Spanish Patience With Flat Cache

**Status:** Open; consequence of issue #3 above
**Impact:** Benchmark comparisons between flat cache and LRU are incomplete

All benchmark results in `docs/cache-redesign/flat_cache_branch_summary.md` use game
types that do not require suit-symmetry (FreeCell, Klondike without suit-symmetry,
Somerset). Spanish Patience and other suit-symmetry games automatically run on the LRU
cache (see issue #3), so they are absent from any flat-vs-LRU performance comparison.

The benchmarking work on a future branch should explicitly include a suit-symmetry game
type once suit-canonical hashing is implemented in the flat cache.

### 5. Benchmarking Infrastructure Untested on Linux

**Affected file:** `scripts/compare_benchmarks.py`
**Status:** Open; code paths implemented but untested
**Impact:** Cross-platform benchmark comparisons may fail on Linux systems

The enhanced benchmarking system uses `/usr/bin/time` for external timing and memory measurement:
- macOS path uses `time -l` format (verified working)
- Linux path uses `time -v` format (code written but untested)

**What needs testing:**
- `/usr/bin/time -v` output parsing for user/system/wall times
- Memory extraction from "Maximum resident set size (kbytes)" format
- Verification that KB→bytes conversion is correct
- Integration with legacy reference solver calibration on Linux

**Workaround:** Use `--legacy-reference` flag is discouraged on Linux until tested.

### 6. Internal Memory Metrics Unreliable for Modern Solver

**Affected file:** `src/main/evaluation/benchmark.cpp`
**Status:** Open; metrics recorded but accuracy questionable
**Impact:** In-process memory reporting may overestimate actual RAM usage

The benchmark engine uses `getrusage().ru_maxrss` for per-iteration memory reporting:
- macOS: reports ~3.2 GB (virtual memory) for Klondike benchmarks
- Actual resident set size (from `/usr/bin/time`): ~1.1 MB

**Root cause:** `ru_maxrss` on macOS reports virtual address space, not physical RAM.

**Current impact:** Memory metrics in individual iteration results are inflated but consistent.

**Recommendation:** For accurate memory profiling, compare system-reported values:
```bash
python3 scripts/compare_benchmarks.py \
    --reference-exe <stable_solver> \
    -- <benchmark_args>
# Check HNF calibration output for system-measured "Peak memory"
```

**Future fix:** Could add `/usr/bin/time` measurement to modern solver benchmarks for accuracy.

### 16. FreeCell Seed 1 Flat-Only Hits Investigation (RESOLVED — commit 4c4b022)

**Investigation:** Recovered 100+ `lru=MISS, flat=HIT` mismatches at op 221+ from previous conversation
**Status:** RESOLVED — Not a bug; legitimate behavior confirmed
**Impact:** None — no correctness issue; safe for Phase 1 implementation

During investigation of cache correctness, previous test run (MismatchAnalyzer.FreeCellSeed1)
reported hundreds of flat-only hits starting at operation 221. Investigation confirmed:

1. **Root cause:** Pile ordering differences between LRU and flat cache
   - When `force_lru=false`: Empty tableau pile order is NOT canonicalized
   - LRU treats different pile orders as different states (different hash)
   - Flat cache treats identical payloads as identical (pile order irrelevant)
   - Result: Flat cache finds duplicates LRU misses → `flat=HIT, lru=MISS`

2. **Verification:**
   - Mismatches only appear with `force_lru=false` (MismatchAnalyzer test)
   - Zero mismatches with `force_lru=true` (DualCacheTest) ✓
   - Behavior matches documented fix in `human_contributions.md` section 22
   - All states are genuinely identical except for tableau pile order

3. **Conclusion:** Legitimate deduplication, not a false positive or regression

**Details:** See `docs/investigation/INVESTIGATION_COMPLETE.md` for full analysis.
Phase 1 implementation can proceed safely — flat cache is trustworthy as oracle.
Investigation commit: `4c4b022`

### 8. Redundant Hash/Payload Computation in Default Binary for LRU Games

**Status:** Short-term fix in Phase 3 (boolean guard); long-term fix deferred  
**Impact:** Performance — wasted hash and payload computation on every DFS move for games routed to `lru_cache` (2-deck, spider, suit-symmetry, accordion)  
**Proposal doc:** `docs/proposals/PROPOSAL-templated-game-state-dispatch.md`

In the default `solvitaire` binary, `game_state` currently computes the Zobrist descriptor hash and `compact_state` payload on every move regardless of which cache is in use at runtime. For the ~40% of game types that route to `lru_cache`, this work is entirely dead — `lru_cache` never reads the hash or payload.

**Short-term fix (Phase 3):** Two boolean flags set once at game_state construction: `computing_flat_hash` (true for all flat-cache variants including hash-only and predecessor) and `computing_flat_payload` (true for flat and predecessor, false for hash-only and LRU). All hash updates and all `payload.set_*` calls (which maintain descriptor state needed for hash XOR) are guarded by `computing_flat_hash`. Only cache-key use of the payload (`set_payload_depth`, `assert_payload_consistent`) is guarded by `computing_flat_payload`. Both are stable branches that the CPU predicts perfectly after the first iteration of any game.

**Long-term fix (deferred):** Template `game_state_impl<Policy>` on a hash/payload policy struct. Four concrete policies (Flat, HashOnly, Predecessor, LRU) are instantiated in the default binary. The runtime dispatch happens once per solve at the `solve_game()` entry point; inside the DFS loop there are zero branches and zero dead stores. The variant binaries (`solvitaire-flat` etc.) become trivial typedef selections of a single policy. Full details, costs, and migration strategy are in `docs/proposals/PROPOSAL-templated-game-state-dispatch.md`.

**Prerequisite for long-term fix:** Phase 3 workpackage complete. The boolean guards introduced in the short-term fix mark every site that will become a policy dispatch call, making the migration mechanical.

### 14. Descriptor State Tracked Only Inside `compact_state payload`

**Status:** RESOLVED — commits 7e73ab1, c4dc519, 76aaa43 (branch `fix/variant-build-hash-only`, merged to `dev`)
**Plan:** `docs/fix-variant-build-hash-only/implementation_plan.md`  
**Impact:** Code clarity — `payload` served a dual role: (1) cache key stored in `flat_cache`, and (2) internal tracking store for old descriptor values needed to compute Zobrist XOR deltas

The 52 card descriptors (and foundation/hole/waste header fields) used to compute the incremental Zobrist hash had no storage of their own — they lived exclusively inside `compact_state payload`. Every `update_*` helper read the old value via `payload.get_*()` before XOR-ing, then wrote the new value via `payload.set_*()`. This meant `payload` had to be maintained even in `hash_only_cache` mode, where the cache itself never stores or reads the payload.

**Fix:** `card_descriptor` enum extracted to `descriptor.h` (commit 7e73ab1). New `hash_descriptor_store` (plain byte arrays, 58 bytes) introduced as the old-value store for the hash-only path (commit c4dc519). `game_state` now uses `hash_descriptor_store hash_desc` instead of `compact_state payload` when compiled with `SOLVITAIRE_HASH_ONLY`, and `compact_state.h` is excluded entirely from that compilation unit (commit 76aaa43). All four `update_*` helpers and `make_move` dispatch via `#ifdef SOLVITAIRE_HASH_ONLY`.

### 15. `dual_cache` and Test Construction Always Enable Both Policy Flags

**Status:** Intentional workaround; deferred clean-up  
**Impact:** Minor — `DualCacheTest` game_states compute hash and payload even for games that would route to LRU in production; test correctness requires this

`game_state` constructors accept an optional `cache_type` string (default `""`) that drives `computing_flat_hash` and `computing_flat_payload`. Two construction paths don't supply a `cache_type`:

1. **Initializer-list constructor** — used heavily in unit tests; no cache context available.
2. **`dual_cache` / `DualCacheTest`** — tests two caches simultaneously without specifying which type drives the game_state policy.

When `cache_type == ""`, both `needs_flat_hash()` and `needs_flat_payload()` return `true` unconditionally, preserving the pre-P3 behaviour where hash and payload were always computed. This means `DualCacheTest` wastes a little work on the LRU side, but it is correct.

**Ideal fix:** Pass explicit policy flags (or a `cache_type`) from `dual_cache` construction sites, computing the OR of the flags required by each constituent cache. Deferred — requires refactoring `dual_cache` and its test harness.

### 11. Build Script Does Not Build Variant Binaries for Regression Tests

**Affected file:** `build.sh`
**Status:** RESOLVED — commit 0653486 (branch `fix/variant-build-hash-only`, merged to `dev`)
**Plan:** `docs/fix-variant-build-hash-only/implementation_plan.md`
**Impact:** Running regression tests required manual build commands; `./build.sh` alone was insufficient

The CMakeLists.txt defines three variant executable targets (`solvitaire-flat`, `solvitaire-hash-only`, `solvitaire-lru`) configured with compile-time cache selection flags. The regression test harness (CMakeLists.txt lines 225–288) invokes these three binaries.

**Fix:** `build.sh` now accepts a `--variants` flag that builds all three variant targets after the main build. `scripts/container-build.sh` forwards `--variants` to the inner build and runs the variant regression tests. Usage:
```bash
./build.sh --variants
./scripts/container-build.sh --variants
```

### 12. Hash-Only vs Flat Cache Total Memory Usage Discrepancy Under Investigation

**Status:** Open; requires further investigation  
**Impact:** Memory efficiency claims for hash-only cache not yet confirmed under realistic benchmarks

**Observed:** Cache cluster allocations are correct in theory:
- Hash-only clusters: 16 bytes (two uint64_t hashes)
- Flat cache clusters: 64 bytes (two 32-byte compact_state entries)
- Expected ratio: 1:4 (hash-only should use 1/4 the cache memory)

Measured allocations match theory (e.g., 50M clusters: 800 MB vs 3,200 MB).

**Discrepancy:** Benchmarks run on Linux report total solver process memory as equal between hash-only and flat cache variants, contradicting the 4× theoretical difference.

**Possible explanations:**
- Game_state allocation overhead (32-byte payload allocated in every game_state regardless of cache type)
- Other per-game overhead that scales equally
- Measurement differences (RSS vs virtual memory vs actual physical allocation)
- Linux/macOS differences in memory reporting

**What needs investigation:**
1. Clarify what metric the benchmark is measuring (total RSS, peak RSS, virtual memory, etc.)
2. Profile actual memory layout with real benchmarks
3. Determine if game_state payload overhead dominates total memory usage
4. Verify whether Linux and macOS show the same ratio or differ

**Related issue:** Issue #9 (payload tracking overhead) may be contributing to total memory overhead.

---

### 13. Per-Variant Oracles for `solvitaire-hash-only` Node Counts (Option B partially done)

**Status:** Levels 1–4 done; Level 5 still uses `--compare-outcome-only`
**Impact:** Level 5 hash-only node counts are not validated against an oracle

`hash_only_cache` uses 16-byte clusters (hash only, no payload / descriptor) and therefore explores states in a different order than `flat_cache`. Per-variant oracles (`tests/oracles/levelN_hash_only.json`) were generated for levels 1–4 using the `pre-refactor-work` tagged binary with `--cache-type hash-only` as the reference. Node counts are now validated for those levels.

**Remaining:** `regression_level5_hash_only` still passes `--compare-outcome-only` against `level5.json` (no level 5 hash-only oracle generated). Generate `level5_hash_only.json` using the same approach when needed.

This approach generalises: `solvitaire-flat` and `solvitaire-lru` could have per-variant oracles generated similarly if node-count validation is desired for those variants.

### 17. Byte-Array Descriptor Store Not Yet Used on Flat-Cache Path

**Status:** Open; deferred post-merge optimisation
**Impact:** Potential performance — flat-cache path could avoid nibble bit-operations on every Zobrist update

`hash_descriptor_store` (introduced in `fix/variant-build-hash-only`) stores descriptors as
a plain byte array (one byte per card, 52 bytes total). `compact_state` packs them as nibbles
(4 bits per card, 26 bytes for 52 cards). Every call to `get_descriptor`/`set_descriptor` in
the flat-cache path therefore requires a shift-and-mask. These are on the hot Zobrist update
path, executed millions of times per solve.

Using `hash_descriptor_store` (or a similar byte-array store) for the flat-cache path too
would eliminate these bit operations. The trade-off: 26 extra bytes per `game_state` on the
DFS stack, and the flat cache clusters continue to use `compact_state` format — so the
`game_state` update path would write to a byte-array store while the cache-insert path would
separately copy into a `compact_state` for the actual cache key.

Magnitude TBD — benchmark before acting. Only worth doing if profiling shows nibble
operations are a measurable fraction of total solve time.

---

## Resolved Issues (for reference)

The following issues were open during development and are now fixed. Full details
are in `docs/resolved-bugs/`.

| Bug | Fix commit | Details |
|---|---|---|
| STARTING(0) not position-canonical (false negatives) | `52b8b63` | `bug_starting_descriptor_not_position_canonical.md` |
| ROOT descriptor overloaded (false positives) | `52b8b63` | `bug_root_descriptor_false_positives.md` |
| Waste pointer stale on regular moves (FortunesFavor) | `52b8b63` | `bug_waste_ptr_and_canfield_wrapping.md` |
| Canfield wrapping builds not recognised (CanfieldStrict) | `52b8b63` | `bug_waste_ptr_and_canfield_wrapping.md` |
| `sol_rules` uninitialized bools (UBSan) | `cb9d26d` | `implementation_plan_v4.md` §M5 |
| `recompute_payload_from_scratch()` four bugs | `3d5f66d` | `implementation_plan_v4.md` §M5 |
| `--force-lru` pile ordering not restored in M6 Phase 1 | `a7f3744` | `implementation_plan_v4.md` §M6 |
| FreeCell seed 1 flat-only hits (op 221+) — investigated 2026-04-10 | `4c4b022` (investigation, not a bug) | `investigation/INVESTIGATION_COMPLETE.md` |
| Build script omits variant binaries (#11) | `0653486` | `docs/fix-variant-build-hash-only/implementation_plan.md` |
| `compact_state payload` dual-role in hash-only path (#14) | `7e73ab1`, `c4dc519`, `76aaa43` | `docs/fix-variant-build-hash-only/implementation_plan.md` |
