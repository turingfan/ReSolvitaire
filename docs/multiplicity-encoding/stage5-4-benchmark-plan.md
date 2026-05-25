# Stage 5.4 — Multiplicity Benchmark Plan

**Date:** 2026-05-24
**Branch:** `multiplicity-encoding`
**Status:** Ready to execute (Stages 0-5.3 complete, all tests pass)

---

## Objectives

Measure the performance of the multiplicity cache against existing cache types across
four comparison axes, from easiest-to-interpret to most research-critical:

| # | Comparison | Games | Expected outcome | Why it matters |
|---|---|---|---|---|
| A | Incremental vs from-scratch multiplicity | Symmetric games | Incremental wins | Validates the Stage 4-5 optimisation |
| B | Flat vs multiplicity (no symmetry) | Flat-eligible games | Flat wins (smaller clusters) | Quantifies the overhead of 64B multiplicity payload vs 32B compact_state |
| C | LRU vs multiplicity (no symmetry) | Flat-eligible games | Multiplicity wins | Shows multiplicity replaces LRU on non-symmetric games |
| D | LRU vs multiplicity (with symmetry) | Suit-symmetric games | Multiplicity wins (hopefully) | **The critical test** — the whole point of the multiplicity encoding |

---

## Comparison A: Incremental vs From-Scratch Multiplicity

**Purpose:** Confirm that incremental updates (Stage 4-5) are faster than calling
`recompute_all()` on every move.

**Method:** Two runs of the same binary, one with incremental enabled (default) and
one with incremental disabled. Since there is no runtime flag to disable incremental
updates, this requires building two binaries:

- **Binary 1 (incremental):** `solvitaire` built from current `multiplicity-encoding`
- **Binary 2 (from-scratch):** `solvitaire` built with `incremental_update_none()` and
  `incremental_update()` replaced by `recompute_all()` calls

**Implementation option:** Add a `--multiplicity-from-scratch` flag or a compile-time
`#define MULTIPLICITY_NO_INCREMENTAL` that forces `recompute_all()` on every move.
The compile-time option is simpler and avoids runtime overhead from the flag check.

**Games and seeds:**

| Game | Seeds | Streamliner | Symmetry mode | Timeout |
|---|---|---|---|---|
| klondike | 1-150 | suit-symmetry | COLOUR | 120s |
| free-cell | 1-150 | suit-symmetry | SUIT_IRRELEVANT | 120s |
| black-hole | 1-150 | auto-foundations | SUIT_IRRELEVANT (inherent) | 120s |

**Validation:** `states_searched` must be identical between incremental and from-scratch
for every seed (same hash → same cache decisions → same search tree). Any mismatch
is a bug.

**Metrics:** Wall-clock time, states/second.

---

## Comparison B: Flat vs Multiplicity (No Symmetry)

**Purpose:** Quantify the overhead of the multiplicity cache on games where the flat
cache already works. The multiplicity cache has 128-byte clusters (2 × 64B entries)
vs flat's 64-byte clusters (2 × 32B entries), so we expect flat to win on cache
efficiency.

**Method:** Compare `solvitaire-flat` (flat cache binary) vs `solvitaire --cache-type
multiplicity`.

**Games and seeds:**

| Game | Seeds | Streamliner | Timeout |
|---|---|---|---|
| klondike-deal-1 | 1-500 | none | 120s |
| free-cell | 1-500 | none | 120s |
| bakers-game | 1-500 | none | 120s |
| canfield | 1-500 | none | 120s |
| somerset | 1-500 | none | 120s |
| black-hole | 1-500 | auto-foundations | 120s |

These are all flat-eligible (no suit-symmetry streamliner, no TABLEAU_PILES).

**Validation:** Solvability verdicts must agree. `states_searched` will differ after
first eviction (different hash functions → different bucket placement → different
eviction patterns).

**Metrics:** Wall-clock time, states/second, solve rate (if timeouts differ).

---

## Comparison C: LRU vs Multiplicity (No Symmetry)

**Purpose:** Show that the multiplicity cache is faster than LRU on games where both
apply. LRU has overhead from pile-order canonicalisation and Boost MultiIndex.

**Method:** Compare `solvitaire-lru` (LRU binary, `--force-lru`) vs `solvitaire
--cache-type multiplicity`.

**Games and seeds:** Same as Comparison B.

**Metrics:** Wall-clock time, states/second, solve rate.

---

## Comparison D: LRU vs Multiplicity With Symmetry — THE CRITICAL TEST

**Purpose:** This is the payoff. The multiplicity cache enables suit-symmetry
canonicalisation in the flat cache, which was previously only possible with LRU.
If multiplicity + symmetry is faster than LRU + symmetry, the entire multiplicity
encoding project delivers value.

**Method:** Compare `solvitaire-lru --streamliners suit-symmetry` vs `solvitaire
--cache-type multiplicity --streamliners suit-symmetry`.

**Games and seeds:**

| Game | Seeds | Streamliner | Symmetry | Timeout | Notes |
|---|---|---|---|---|---|
| klondike | 1-500 | suit-symmetry | COLOUR (26 classes of 2) | 300s | Core test case |
| klondike-deal-1 | 1-500 | suit-symmetry | COLOUR | 300s | Variant |
| free-cell | 1-500 | suit-symmetry | SUIT_IRRELEVANT (13 classes of 4) | 300s | Stronger symmetry |
| black-hole | 1-500 | auto-foundations | SUIT_IRRELEVANT (inherent) | 300s | Inherent symmetry, no explicit streamliner needed |

**Also test TABLEAU_PILES games** (multiplicity-only, no flat/LRU comparison possible
without TABLEAU_PILES support in LRU):

| Game | Seeds | Symmetry | Timeout | Notes |
|---|---|---|---|---|
| east-haven | 1-500 | NONE | 120s | Multiplicity vs flat baseline |
| spiderette | 1-500 | NONE | 120s | Multiplicity vs flat baseline |

For TABLEAU_PILES games, compare multiplicity (with `in_space(k)` pile-indexed
dedup) vs flat (no pile dedup — these games currently fall through to LRU on `dev`).

**Validation:** Solvability verdicts must agree between LRU and multiplicity.

**Metrics:** Wall-clock time, states/second, solve rate, PAR2 score.

---

## Execution Plan

### Prerequisites

1. Build release binaries on the remote machine:
   ```bash
   git checkout multiplicity-encoding
   git pull
   ./build.sh --release --unit-tests
   # Quick sanity check
   cd cmake-build-release && ctest -R ^unit_tests$ --output-on-failure
   ```

2. Build the from-scratch variant for Comparison A (one of):
   - Option 1: Add `#define MULTIPLICITY_NO_INCREMENTAL` compile flag
   - Option 2: Manually patch `game_state.cpp` to always call `recompute_all()`
   
   Build as a separate binary (e.g. `solvitaire-mult-scratch`).

### Remote Execution

Use `bench --detached` on the remote machine, lodge results with `bench-lodge` locally.

**Setup (one-time on remote):**
```bash
ssh -A user@host 'bash -s' < \
    ~/Research/ReSolvitaire-project/02-Code-Repositories/ReSolvitaire-bench/bootstrap/setup-remote.sh
```

### Experiment Structure

Run experiments in phases to allow early results to inform later decisions.

#### Phase 1: Quick Validation (1-2 hours, 16 workers)

Seed range 1-50, all four comparisons. This catches any issues before committing to
the full run and gives early signal on the expected outcomes.

```bash
# Comparison A: incremental vs from-scratch (3 games × 50 seeds)
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-50 --timeout 120000 \
    --streamliner suit-symmetry \
    --output $BENCH_RUN_DIR/data/A_incr_klondike.csv \
    --label mult-incremental --no-summary \
    -- --cache-type multiplicity

python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire-mult-scratch \
    --type klondike --seeds 1-50 --timeout 120000 \
    --streamliner suit-symmetry \
    --output $BENCH_RUN_DIR/data/A_scratch_klondike.csv \
    --label mult-from-scratch --no-summary \
    -- --cache-type multiplicity

# Comparison D: LRU vs multiplicity with symmetry (critical test)
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire-lru \
    --type klondike --seeds 1-50 --timeout 300000 \
    --streamliner suit-symmetry \
    --output $BENCH_RUN_DIR/data/D_lru_klondike.csv \
    --label lru-symmetry --no-summary \
    -- --force-lru

python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-50 --timeout 300000 \
    --streamliner suit-symmetry \
    --output $BENCH_RUN_DIR/data/D_mult_klondike.csv \
    --label mult-symmetry --no-summary \
    -- --cache-type multiplicity
```

Review Phase 1 results before proceeding. Check:
- Solvability agreement
- `states_searched` match for Comparison A
- Direction of speedup/slowdown

#### Phase 2: Full Run (4-8 hours, 16 workers)

Expand to full seed ranges (1-500) across all comparisons. Use
`benchmark_orchestrator.py` or a custom experiment script.

Recommended approach: write a dedicated experiment script
`scripts/experiments/bench_multiplicity.sh` that runs all four comparisons
in parallel, chunked by game and seed range.

**Parallelism:** With 16 cores, run 16 `run_benchmark.py` workers.
Each worker handles one (game, seed-chunk, cache-type) combination.
`benchmark_orchestrator.py` handles chunking and parallelism.

**Estimated total time** (16 workers):

| Comparison | Games × Seeds | Est. serial time | Est. parallel (16w) |
|---|---|---|---|
| A | 3 × 150 = 450 runs × 2 | ~2h | ~15 min |
| B | 6 × 500 = 3000 runs × 2 | ~4h | ~30 min |
| C | 6 × 500 = 3000 runs × 2 | ~4h | ~30 min |
| D | 4 × 500 = 2000 runs × 2 | ~8h | ~1h |
| **Total** | | **~18h serial** | **~2.5h parallel** |

These are rough estimates assuming ~5s average per run with timeouts. Actual time
depends heavily on game difficulty and timeout frequency.

### Results Collection

```bash
# On remote: bundle results
# (bench --detached handles this automatically)

# On local: lodge into DataLad results repo
scp "user@host:~/bench-bundles/*.tar.gz" ~/bench-bundles/
cd ~/Research/ReSolvitaire-project/04-Results
bench-lodge
```

### Analysis

Use the R analysis suite for each comparison:

```bash
# Per-comparison HTML report
Rscript analysis/benchmark.R \
    --baseline data/D_lru_klondike.csv \
    --current  data/D_mult_klondike.csv \
    --output   reports/D_lru_vs_mult_klondike.html

# Quick summary
Rscript analysis/summary.R data/D_mult_klondike.csv
```

Key metrics to report:
- Geometric mean speedup with 95% bootstrap CI
- PAR2 score comparison (penalises timeouts)
- Wilcoxon signed-rank p-value
- Solve rate difference (if any seeds timeout in one but not the other)
- Per-instance scatter plot

---

## Implementation Needed Before Running

1. **From-scratch binary for Comparison A:** Add compile-time flag
   `MULTIPLICITY_NO_INCREMENTAL` that replaces incremental update calls with
   `recompute_all()`. Build as `solvitaire-mult-scratch` or similar.

2. **Experiment script:** Write `scripts/experiments/bench_multiplicity.sh` that
   orchestrates all four comparisons with appropriate parallelism.

3. **Ensure `--cache-type multiplicity` works with `solvitaire-lru` binary:**
   Currently the LRU-only binary ignores `--cache-type`. For Comparison D, the
   LRU baseline uses `solvitaire-lru --force-lru`, not `--cache-type`. Verify
   this is correct.

4. **TABLEAU_PILES comparison:** Currently TABLEAU_PILES games fall through to
   LRU on `dev`. On the multiplicity branch, `--cache-type multiplicity` enables
   the multiplicity cache for these. Need to verify the LRU fallback for the
   baseline comparison.

---

## Success Criteria

| Comparison | Success | Concern |
|---|---|---|
| A: Incr vs scratch | Incremental ≥2× faster, states_searched identical | If <2×, incremental overhead may not justify complexity |
| B: Flat vs mult | Flat ≤30% faster (acceptable overhead) | If mult is >50% slower, cluster size cost is too high |
| C: LRU vs mult | Mult ≥2× faster | If mult is slower, something is wrong |
| D: LRU vs mult+sym | Mult ≥1.5× faster with comparable solve rate | **The key result.** If mult+sym is slower than LRU+sym, the project doesn't deliver its core value |

---

## Output

Results recorded in:
- `04-Results/` (DataLad, raw CSVs and bundles)
- `docs/multiplicity-encoding/stage5-benchmark-results.md` (summary with key findings)
- HTML reports from R analysis suite
