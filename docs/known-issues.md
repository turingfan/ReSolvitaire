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

**Current mitigation:** The templated dispatch switch in `main.cpp` routes suit-symmetry
games to `solver_impl<LRUPolicy>`, so the solver automatically falls back to LRU.
(`cache_interface.h` and `use_new_cache()` were removed in `feature/templated-dispatch`
Commit 5; the routing is now handled at the dispatch level.)

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

### 7. Accordion Debug Assert Crash (`assert_payload_consistent`)

**Affected test:** `PredecessorDualCacheTest.AccordionAgreement` (deleted in Commit 5; see below)
**Status:** Open; deferred — may become moot if `PredecessorPolicy` is removed in Phase B
**Impact:** In debug builds, accordion moves produce incorrect incremental payload; underlying descriptor update bug still present in code

In debug builds, `assert_payload_consistent()` fires during accordion moves: the incremental
`compact_state` payload diverges from the scratch-recomputed payload. This means
`make_move`/`undo_move` contains a descriptor update bug specific to accordion moves.

Confirmed pre-existing at commit `4c4b022`, before any Phase 0/1 work. Root cause is a bug
in the descriptor update logic for accordion moves — unrelated to the pile-first undo
refactor or templated dispatch. `assert_payload_consistent()` is still compiled in and called
at `solver.cpp:140` (in debug builds only); `generic_flat_cache_test.cpp:14–17` has a comment
warning future developers about this crash signature.

The test that caught it (`PredecessorDualCacheTest.AccordionAgreement`) was deleted in
Commit 5 as part of dual-cache infrastructure removal. The underlying bug is therefore
untested but still present.

**Why deferred:** Accordion uses `PredecessorPolicy`, which was out of scope for Phase 1
(pile-first undo) and Phase A (templated dispatch). Decision to ignore was made in commit
`706cb6d`.

**Pending decision for Phase B:** If `PredecessorPolicy` is retired when the flat cache is
extended to accordion games (Phase B), this bug becomes moot — the buggy descriptor update
code would be removed along with the policy. If `PredecessorPolicy` is retained, this bug
needs fixing before accordion regression tests can be run in debug mode.

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

### 21. ~~Waste Descriptor Causes O(stock) Updates Per Stock Move~~ RESOLVED

**Resolved:** 2026-05-27 (PR #4, merged to `multiplicity-encoding`)

Collapsed `MLD_IN_WASTE` to top-of-waste only; non-top waste cards use `MLD_IN_STOCK`.
`stock_k_plus` incremental update is now O(1) (max 4 descriptor changes). 
`stock_to_all_tableau` remains as fallback — not in scope for this fix.

With this fix, a `stock_k_plus` move changes at most 2 descriptors: the old
top-of-waste (becomes `MLD_IN_STOCK`) and the new top-of-waste (becomes
`MLD_TOP_OF_WASTE`). The k cards dealt between stock and waste all keep
`MLD_IN_STOCK` throughout.

**Note:** The existing waste-deal symmetry logic (`waste_deal_sym` in
`recompute_all()`) already collapses stock and waste to `MLD_IN_STOCK` under
certain conditions. This fix generalises that approach.

**When to address:** After Stage 5 (incremental with symmetry) is working and
validated. The fix is a descriptor-level change that is independent of the
cascade machinery.

### 22. Trace Regression Reference Binaries Need Rebuild (STRACE_EVICT fix)

**Status:** Open; blocking trace regression tests on `multiplicity-encoding` branch
**Impact:** `trace_regression_level1` and `trace_regression_level2` fail because reference
binaries were built before the STRACE_EVICT fix

**Root cause:** `generic_flat_cache.h` was missing `STRACE_EVICT()` calls in its
`do_replacement` overloads. Evictions were counted (`eviction_count++`) but not traced.
This was fixed on `multiplicity-encoding` (adding STRACE_EVICT to all 5 eviction paths
across 3 replacement strategies: `insert_simple_tag`, `insert_depth_tag`,
`insert_predecessor_tag`).

The reference binaries in `05-Executables/reference/` were built from a pre-fix commit
and emit MISS where the current binaries now correctly emit EVICT. The trace regression
comparison (`compare_traces.py` in regression mode) sees this as a divergence.

**Fix:** Rebuild reference binaries from a commit that includes the STRACE_EVICT fix.
This requires the fix to be merged to `dev` first, since reference binaries should be
built from the stable branch.

**Affected tests:** `trace_regression_level1`, `trace_regression_level2` (6 failures
out of 160 instances at level 2)

### 20. Reduced Metamorphic Testing: Flat vs LRU Agreement No Longer Tested

**Status:** Open — test removed; gap acknowledged
**Impact:** Less metamorphic test coverage between flat and LRU cache policies

The old `dual_cache` infrastructure tested that flat and LRU caches agreed on outcomes
for the same instances (`DualCacheTest`). With templated dispatch, `solver_impl<Policy>`
holds `Policy::cache_type&` directly and the dual-cache wrapper is gone.

`FlatVsLRU_FortunesFavor` was removed from `search_trace_agreement_test.cpp` because
flat and LRU produce different search trees in independent runs: `LRUPolicy` has
`skip_pile_ordering=false` (pile order canonicalized), `FlatPolicy` has
`skip_pile_ordering=true` (not canonicalized). Traces diverge from the first move.

The original dual_cache agreement held because both caches shared the same pile-ordered
game state (`force_lru=true`). That invariant is no longer expressible in the templated
architecture without significant extra infrastructure.

`HashOnlyVsFlat_Klondike50Seeds` (50 seeds, `search_trace_agreement_test.cpp`) remains
and provides some cross-policy coverage. Outcome-level agreement between flat and LRU
is verified indirectly by regression oracles (both produce same solve/unsolve results)
but not at the search-event level.

**Possible future fix:** A test that runs both policies and compares outcomes (not traces)
on a shared set of instances, or a mode that forces pile ordering on the flat path.

