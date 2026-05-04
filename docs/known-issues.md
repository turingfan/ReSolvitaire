# Known Issues

This file tracks open issues. Resolved issues are documented in `docs/resolved-bugs/resolved-issues.md`.

---

## Open Issues

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

### 13. Per-Variant Oracles for `solvitaire-hash-only` Node Counts (Option B partially done)

**Status:** Levels 1–4 done; Level 5 still uses `--compare-outcome-only`
**Impact:** Level 5 hash-only node counts are not validated against an oracle

`hash_only_cache` uses 16-byte clusters (hash only, no payload / descriptor) and therefore
explores states in a different order than `flat_cache`. Per-variant oracles
(`tests/oracles/levelN_hash_only.json`) were generated for levels 1–4 using the
`pre-refactor-work` tagged binary with `--cache-type hash-only` as the reference. Node
counts are now validated for those levels.

**Remaining:** `regression_level5_hash_only` still passes `--compare-outcome-only` against
`level5.json` (no level 5 hash-only oracle generated). Generate `level5_hash_only.json`
using the same approach when needed.

This approach generalises: `solvitaire-flat` and `solvitaire-lru` could have per-variant
oracles generated similarly if node-count validation is desired for those variants.

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

### 19. `docs/` Folder Needs Cleanup

**Status:** Open; deferred housekeeping
**Impact:** Stale and duplicated content in `docs/` may cause confusion

The `docs/` folder has accumulated content from multiple development phases and may
contain stale reference docs, directories that should be archived, and duplication
between `docs/resolved-bugs/` and `01-Knowledge-Base/Dev-Logs/resolved-bugs/`. A
deliberate cleanup pass is needed to:
- Archive or remove completed-branch documentation
- Consolidate resolved-bug records into a single canonical location
- Verify all active docs are current and correctly placed per `AGENTS.md`

**Where to address:** Housekeeping session before or after `feature/templated-dispatch`
merges to `dev`.

### 18. Cache Parity Testing Needs New Approach

**Status:** Open; old dual-cache infrastructure deleted in Commit 5
**Impact:** No parity cross-checking between cache implementations until search trace is built

The old `dual_cache` test infrastructure (wrapping two `cache_interface` implementations)
was incompatible with `solver_impl<Policy>` holding `Policy::cache_type&` directly. All
dual-cache test files and the `dual_cache.h` wrapper were deleted in Commit 5 as part of
legacy cache removal.

**Planned replacement:** Search trace infrastructure — instrument `solver_impl<Policy>` to
log moves made (shared notation from `move.h`), cache insert/contains results (hit/miss),
and eviction events. Run two solves with different policies, diff the traces. This is more
powerful than the old approach: also useful for debugging, performance analysis, and
regression diagnosis. Hashes are NOT logged (hashing can legitimately change); move
sequences are the invariant.
