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
