# Known Issues

This file tracks open issues. Resolved issues are documented in `docs/resolved-bugs/resolved-issues.md`.

**Numbering:** Before assigning a new KI number, check `resolved-issues.md` for the
highest number already used. Numbers must be unique across both files.

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

### 24. ~~Waste Descriptor Causes O(stock) Updates Per Stock Move~~ RESOLVED

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

### 25. ~~Trace Regression Reference Binaries Need Rebuild (STRACE_EVICT fix)~~ RESOLVED

**Resolved:** 2026-05-27 (dev commits `5f4a907`, `a1c4c9f`)

Reference binaries rebuilt from `dev` after multiplicity-encoding merge. New binaries
include STRACE_EVICT fix and Stage 6 auto-dispatch. Both `trace_regression_level1` and
`trace_regression_level2` pass on macOS and Linux. Old binaries preserved in
`05-Executables/reference/`. See `05-Executables/reference/README.md` for details.

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

### ~~23. Suit-Symmetry Detection for Hole Games Not Centralised~~

**Status:** **Resolved** (PR #8, merged 2026-05-29)

Fixed by adding `sol_rules::inherent_suit_symmetry()` as a single centralised decision
point. All 4 dispatch `suit_sym` computations, `make_desc_ctx()`, and LRU cache hasher
now use this method. Hole games (black-hole, golf, worm-hole) get suit-symmetry
canonicalization on all cache paths without requiring explicit `--streamliners`.

Impact: worm-hole_325114 dropped from 73.8M to 308 states searched — suit canonicalization
is spectacularly effective for no-build hole games. All oracles regenerated levels 1-5.

### 26. Non-Cache Frontier Memory Dominates RAM on Deep Searches (runaway depth × per-frame `child_moves`)

**Affected:** all cache policies (flat, multiplicity, hash-only, LRU) — solver-wide, not
cache-specific
**Status:** Open; **deferred — large algorithmic change, out of scope for cache work**
**Impact:** OOM-kills of benchmark workers on hard instances (free-cell, somerset).
Peak RSS reaches 20–40 GB.

**Two intertwined causes, both algorithmic and unrelated to the cache implementation:**

1. **Runaway search depth (already known).** On some hard seeds the DFS descends to
   `10⁷–10⁸` moves deep before exhausting a branch. (Related: KI-2 Spanish Patience
   move-ordering regression — same family of "search descends far further than the
   solution length" behaviour.)

2. **Per-frame `child_moves` storage.** The DFS frontier (`solver.cpp`: `std::vector<solver_node> frontier`)
   keeps, on **every** ancestor frame, the *remaining unexplored legal moves* in a
   `std::vector<move> child_moves` (`solver.cpp:188`), so siblings need not be recomputed
   on backtrack. Memory is therefore `O(depth × branching)`. Measured ≈150–220 B/frame
   (free-cell, branching ~15). At depth `1.3×10⁸` this is ~20 GB of **non-cache** memory.

**Evidence (6-worker run `mult_20260531_164838`, 1000 free-cell seeds, `--cache-capacity` default 100M):**
- `corr(peak_RSS, max_depth) = 0.82`.
- flat seed 473: RSS 23.5 GB, depth 134 M → cache mmap ≤3.0 GB ⇒ **≈20.5 GB is non-cache frontier**.
- mult seed 956: RSS 41.7 GB, depth 203 M → cache mmap ≤6.0 GB ⇒ **≈35.7 GB is non-cache frontier**.
- This is **not** multiplicity-specific: paired same-seed `RSS(mult) − RSS(flat)` has
  median ≈0 (both `streamliner=none`). flat and mult use comparable RAM.

**Why deferred:** Fixing either cause is a significant algorithmic change to the DFS
engine, not the cache:
- (1) requires move-ordering / depth-bounding heuristics (see KI-2).
- (2) requires *not* storing `child_moves` and recomputing legal moves on backtrack
  (CPU-for-memory trade), or an iterative move generator — a core solver redesign.

These are recorded here for completeness. **They are out of scope for the current
cache/benchmark investigation.** The cache-specific finding from that investigation (the
dead `cache_state` field carried on flat/mult frames) is tracked separately.

> Note: LRU's large-RAM mechanism on these seeds differs — its `cache_size` fills to the
> 100 M default capacity at ~320 B/entry (≈32 GB of heap cache), so LRU RAM *is* bounded
> by `--cache-capacity`, whereas flat/mult frontier RAM is **not** (it scales with depth).

### 27. `trace_regression_level1/2` Diverge From a Stale Reference Binary

**Affected:** Gate 2 (trace) CTest targets `trace_regression_level1` (150 instances) and
`trace_regression_level2` (160). Branch: `benchmark-rationalisation` (likely `dev` too).
**Status:** **RESOLVED on mac (2026-06-03) by re-baseline; Linux binary built, pending
verification on a Linux host.** Triage confirmed it was hypothesis (1) — legitimate
path-only drift (KI-23 / Stage-6 multiplicity suit-symmetry), not a regression: outcomes
correct, and current node counts match the validated level-1 oracle (which the
`regression_level1` gate enforces ×4). Re-baselined `TRACE_REF_BIN` to fresh `…-20260603-7eb5883`
binaries (commit `36162ea`); `trace_regression_level1` (36s) + `level2` (184s) now pass on
mac. Linux arm64 reference built via `container` (unverified on a Linux host yet; both
binaries live untracked in `05-Executables/reference/` for out-of-band distribution).
*Original report below for context.*
**Impact:** The trace gate cannot pass on this branch, so it can't gate commits until
resolved. No known correctness impact (see evidence below).

**Symptom:** Both targets fail with `diverges at event N` across many game types
(alpha-star, black-hole, delta-star, eight-off, fore-cell, klondike-deal-*, free-cell,
somerset, accordion, worm-hole, …), often at very early events (4, 8, 12).

**Evidence that the current binary is NOT the problem — the reference is stale:**
- The failures are **identical with and without** the 2026-06-01 `cache_state` layout fix
  (verified by `git stash` + rebuild + rerun). So no recent solver change caused them.
- `trace_identity_flat`, `trace_identity_lru`, `trace_until_timeout`,
  `SearchTraceAgreementTest.*`, all `^unit_tests$`, and `regression_level1` (default /
  flat / hash-only / lru) **all pass** — the current binary is internally deterministic,
  flat/LRU-agreeing, and outcome-correct against the oracles.
- The CMake default `TRACE_REF_BIN` is `05-Executables/reference/solvitaire-trace-reference-mac-arm64`
  (dated **2026-05-06**). The newer dated reference is from `dev @ 9673fd3` (2026-05-29),
  but `9673fd3` is **NOT an ancestor of current HEAD** — dev history was reshaped by PR
  merges (e.g. #8), so the reference predates / diverges from current branch behaviour.

**Hypotheses (unverified — this is the "come back to it" part):**
1. The search-event sequence changed *legitimately* since the reference was built
   (candidates: KI-23 suit-symmetry centralisation `629f07f`, Stage-6 multiplicity
   auto-dispatch `acbc8bf`, or other post-`9673fd3` work) → fix = **re-baseline** the
   trace reference binaries (procedure in `05-Executables/reference/README.md`).
2. A genuine trace-level regression slipped in between the reference commit and HEAD →
   fix = find and repair it.
3. The reference was built from a squashed/divergent history and never matched this
   branch → fix = re-baseline.

**Next step when revisited:** rebuild a trace reference from the current HEAD, diff a
single divergent instance (e.g. `alpha-star_seed_4` at event 8) between current and
reference traces with `scripts/compare_traces.py --full`, and decide between re-baseline
(hyp. 1/3) and bug-fix (hyp. 2). Until then the trace *regression* sub-gate is known-red;
the rest of Gate 2 (identity, agreement, until-timeout, unit tests) is green.

### 29. Level 4/5 Outcome-Only Regression Is Untrustworthy With Unsound Streamliners

**Affected:** `regression_level4` and `regression_level5` (and their variant targets) —
the CTest targets that compare **`solution_type` only** (no `--enforce-node-counts`).
**Status:** Open; **needs a methodology decision — do not trust L4/L5 outcome pass/fail
for unsound-streamliner instances.**

**Root cause:** Many oracle instances run with `streamliner = both` (auto-foundations +
suit-symmetry). **`both` is unsound** (it can report a winnable deal as `unsolvable`, and
the result depends on search ordering). So the recorded `solution_type` for such an
instance is **not stable ground truth** — a different search (different build, machine,
cache policy, or even node-order change) can legitimately flip `solved`↔`unsolvable`.
An outcome-only comparison therefore yields spurious failures (or false passes).

**Evidence (2026-06-02):** regenerating the L2 oracle flipped
`klondike-deal-8_316_winnable` from `solved` to `unsolvable` under `both`, purely from
search reorganisation accumulated since the oracle was last baselined (KI-23 suit-symmetry
centralisation etc.) — not a solver bug, just the unsoundness surfacing.

**What IS fine:** the exact-duplication levels (`regression_level2/3`, which use
`--enforce-node-counts`) are reliable — for a pinned binary the search is deterministic, so
nodes *and* outcome reproduce exactly. The problem is specifically **outcome-only**
comparison (L4 base, L5).

**Options (for the deliberate decision):**
1. Drop L4/L5 outcome regression entirely (cannot be trusted as written).
2. Restrict outcome comparison to **sound** streamliners (`none`) only.
3. Switch L4/L5 to deterministic cutoffs (`--max-states`) + `--enforce-node-counts`, making
   them exact-duplication like L2/L3 (reproducible, but build/machine-pinned and expensive).

**Status of oracles (2026-06-02):** L2/L3 oracles were regenerated under the new CPU-time
`--timeout` and committed (all-definitive, reproducible). **L4/L5 oracles were deliberately
NOT regenerated** pending this decision (and L4 regen on a 32 GB box is memory-risky per
KI-28). See also the CPU-time timeout switch and KI-26/27/28.

### 28. Benchmark Worker Sizing Cannot Bound a Single Runaway Worker (cache-based budget insufficient under a cgroup cap)

**Affected:** `scripts/bench_lib/concurrency.py` worker-count sizing; any parallel
benchmark run (`benchmark_orchestrator.py`, `bench_multiplicity.sh`) on a host with a
fixed memory ceiling (notably a cgroup `memory.max`).
**Status:** Open; **deferred (worker-safety hardening) — logged for a later pass.**
**Impact:** OOM-kills on a shared/capped Linux box even when the worker count looks safe;
the kernel's memcg OOM killer may kill *innocent* sibling workers, not the greedy one,
so kills appear scattered across unrelated seeds.

**Root cause:** `concurrency.py` budgets per-worker memory by **cache type × capacity**
(the flat mmap reservation, or LRU per-entry). But per KI-26 the dominant per-worker term
on hard instances is the **non-cache DFS frontier**, which scales with search depth and is
**not bounded by the cache**. A single runaway-depth seed reached **20–40 GB** in the
6-worker run (`mult_20260531_164838`: free-cell 956 → 41.7 GB). So:
- The cache-based budget under-counts the real peak by an order of magnitude on the tail.
- Under a fixed cgroup `memory.max` (confirmed on the remote box: `user-25002.slice`
  `memory.max = 64 GiB`, and `memory.peak` reached it exactly), two or three workers
  landing on deep seeds together exceed the slice limit and trigger a memcg OOM.

**Confirmed environment fact (2026-06-02):** the binding limit on the remote host is a
cgroup v2 `memory.max = 68719476736` (64 GiB) on `user.slice/user-25002.slice` — invisible
to `ulimit -a` (which reports rlimits, all "unlimited" here). `memory.peak == memory.max`
to the byte, i.e. runs drove the slice to its ceiling.

**Proposed fix (when revisited):** bound each worker *independently* so a runaway dies
cleanly in its own scope instead of taking down the slice (and siblings):
- `systemd-run --user --scope -p MemoryMax=<N>G -p MemorySwapMax=0 <solver…>` per worker
  (clean per-process memcg kill → solver/wrapper reports `terminated`/`KILLED` for that
  one seed only), or
- `ulimit -v <kbytes>` in the worker (caps virtual address space; must be set above the
  flat-cache mmap reservation so it doesn't kill at startup).
- Then derive worker count from `effective_memory_limit()` ÷ the per-worker `MemoryMax`,
  not from the cache reservation alone.

Until done, mitigate manually: fewer workers, and/or a modest `--cache-capacity`, and/or
run under a per-worker `systemd-run … MemoryMax`. Relates to [[flat-cache-dedup-divergence-bug]]
(KI-26) and the benchmark timeout/kill methodology.

