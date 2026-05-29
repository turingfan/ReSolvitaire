# Surviving Benchmark Script Inventory

**As of:** 2026-05-29 (Stage 1 Wave A complete)
**See also:** [START-HERE.md](START-HERE.md) for usage examples, [csv_schema.md](csv_schema.md) for the CSV contract.

Status key:
- `core` — spine of the pipeline; every benchmark run goes through these.
- `experiment` — bespoke per-experiment script; shells out to core.
- `stage2-eval` — overlapping comparison/collection helpers deferred to Stage 2 for evaluation (kept in place, no decision made yet).
- `dedup-candidate` — one of a redux/unwinnable pair kept pending Stage 2 (see note below).
- `analysis` — R analysis scripts; canonical analysis path.
- `out-of-scope` — test/trace infrastructure, not benchmarking.

---

## Core pipeline

| Script | One-line job | Inputs | Outputs | Status |
|---|---|---|---|---|
| `scripts/run_benchmark.py` | Runs the solver on a seed range or set of JSON instance files, times each run with `perf_counter`, measures peak RSS via `/usr/bin/time`, and writes the 21-column CSV. | `--solver` binary, `--type`/`--instances`, `--seeds`, `--timeout`; optional `--streamliner`, `--cache-capacity`, `--label`, `--warmup`, `--iterations` | One CSV row per timed run; optional JSON sidecar via `--output-json` | `core` |
| `scripts/benchmark_orchestrator.py` | Parallel fan-out over `run_benchmark.py` across multiple game types and solver variant binaries; merges per-chunk CSVs into a single `combined.csv` for the bench hook. | `--solver-dir` (variant binaries) or `--solver`; `--workers`, `--output-dir`; optional `--games`, `--seeds`, `--timeout` | `<output-dir>/combined.csv` + per-chunk CSVs | `core` |
| `scripts/oracle_to_benchmark_cmds.py` | Reads a regression oracle JSON and emits one `run_benchmark.py` shell command per matching entry (first with header, rest with `--no-header --append`), for piping to `bash`. | `--oracle` JSON file, `--solution-type` filter, `--solver`, `--timeout`, `--warmup`, `--iterations`, `--output`; optional solver flags after `--` | Shell commands printed to stdout | `core` |

---

## Bespoke experiment scripts

| Script | One-line job | Inputs | Outputs | Status |
|---|---|---|---|---|
| `scripts/experiments/bench_multiplicity.sh` | Runs four phases (A–D) comparing multiplicity cache against flat and LRU variants, chunking seeds and parallelising via `xargs -P`; shells out to `run_benchmark.py` per chunk. | `--phase`, `--seeds`, `--games`, `--workers`, `--timeout`, `--dry-run`; reads binaries from `cmake-build-release/bin/` | Per-phase CSV files in `$BENCH_RUN_DIR` | `experiment` |
| `scripts/experiments/tuesday-night-redux.sh` | Runs 35 flat-cache-eligible game types (50 seeds, warmup 1, median of 3, 60 s timeout) via `run_benchmark.py` and merges into `combined.csv`; intended to be run through `bench`. | `$SOLVER`, `$SEEDS`, `$TIMEOUT`, `$WARMUP`, `$ITERATIONS`, `$BENCH_RUN_DIR` (set by bench) | `$BENCH_RUN_DIR/combined.csv` | `experiment` |
| `scripts/experiments/bench_level5_unwinnable.sh` | Benchmarks all flat-eligible unwinnable Level 5 instances across four solver variants (default, flat, lru, legacy) using `oracle_to_benchmark_cmds.py`. | `$RESULTS_DIR` (positional, optional); reads `tests/oracles/level5.json`; expects binaries in `cmake-build-release/bin/` and legacy binary at `$LEGACY_BIN` | Per-variant CSV files in `$RESULTS_DIR` | `experiment` |

**Note on redux/unwinnable pairs:** `tuesday-night-redux2.sh` and `bench_level5_unwinnable2.sh` are dedup-candidates (see below). The scripts above are the kept copies of each pair.

---

## Stage 2 evaluation group (overlapping helpers — no decision yet)

These scripts overlap on baseline-vs-current comparison and result collection.
Per the rationalisation plan, no hasty deletion in Stage 1. They are inventoried
here and left in place; the canonical-path decision is explicit Stage 2 work.

| Script | One-line job | Inputs | Outputs | Status |
|---|---|---|---|---|
| `scripts/compare_benchmarks.py` | Reads two benchmark result sets (JSON report files), computes speedup / NPS metrics, and prints comparison tables; also carries a legacy benchmark runner. Marked DEPRECATED in source; relationship to R analysis layer under evaluation. | Legacy JSON report directories or benchmark run paths | Text/markdown comparison tables to stdout | `stage2-eval` |
| `scripts/benchmark_baseline.sh` | Records flat-cache nodes/second for all flat-cache games using the solver's built-in `--benchmark` mode; prints a table and saves JSON. | `--exe`, `--seeds`, `--iterations`, `--timeout-ms`, `--out`; calls solver `--benchmark` mode directly | `results/throughput_baseline_<date>.json` | `stage2-eval` |
| `scripts/benchmark_speedup.sh` | Compares flat cache vs `--force-lru` on a fixed game set using the solver's built-in `--benchmark` mode; prints a speedup table. | `--exe`, `--seeds`, `--iterations`, `--timeout-ms`; calls solver `--benchmark` mode directly | Speedup table to stdout | `stage2-eval` |
| `scripts/generate_baseline.py` | Runs the solver on Level 1 JSON instance files to produce the Level 1 regression oracle JSON, optionally using ground-truth AAA-files to select the correct streamliner per instance. | `--exe`, `--instances` dir, `--output` oracle JSON, optional `--data-dir` (paper data) | `tests/oracles/level1.json` (or specified path) | `stage2-eval` |
| `scripts/collect_results.sh` | SSHes to a remote machine, tars up a results directory, and copies the tarball to the local machine. | `--host`, `--remote-dir`, `--remote-root`, `--local-dir` | Tarball in `$LOCAL_DIR` | `stage2-eval` |
| `scripts/compare_binaries.sh` | Validation harness: runs `solvitaire`, `solvitaire-flat`, `solvitaire-hash-only`, and `solvitaire-lru` on the same seeds and checks that `solution_type` (outcome) agrees across all four. Requires `jq`. | Hardcoded game types (`klondike free-cell`), seed range, timeout; reads from `cmake-build-release/bin/` | Pass/fail report to stdout | `stage2-eval` |
| `scripts/extract_benchmark_results.py` | Reads legacy JSON benchmark report files (`report-*.json`) from a directory, extracts NPS metrics, computes speedup, and writes a markdown summary table. | `benchmark_dir` positional arg (default `benchmarks/TuesdayNight/`), optional `--output`, `--verbose` | Markdown table to `BENCHMARK_RESULTS.md` in the benchmark dir | `stage2-eval` |

---

## Dedup candidates (kept pairs — Stage 2 decision)

The following scripts exist as `v1` / `v2` pairs. The purpose of the pairs is no longer
remembered. Both are kept for safety (per Ian, 2026-05-29); only the `2` variant will be
removed after Stage 2 confirms it adds nothing the `v1` copy does not.

| Script | Kept as | Notes |
|---|---|---|
| `scripts/experiments/tuesday-night-redux2.sh` | dedup-candidate (`v2` of `tuesday-night-redux.sh`) | `v2` runs four solver variants (lru, flat, default, hash-only) per game; `v1` runs only the default binary. Different scope — Stage 2 should decide which is canonical. |
| `scripts/experiments/bench_level5_unwinnable2.sh` | dedup-candidate (`v2` of `bench_level5_unwinnable.sh`) | File content appears identical to `v1` (same header, same structure). Stage 2 should diff and confirm before deleting. |

---

## Analysis layer

| Script | One-line job | Inputs | Outputs | Status |
|---|---|---|---|---|
| `analysis/summary.R` | Quick summary of a single benchmark CSV (solve rate, geo-mean time, PAR2, NPS); also called automatically by `run_benchmark.py`. | CSV or JSON benchmark file | Printed summary table | `analysis` |
| `analysis/benchmark.R` | Full HTML comparison report between a baseline and current CSV (per-instance scatter plots, geo-mean speedup, PAR2 comparison). | `--baseline`, `--current` CSVs, `--output` HTML path, optional `--metric`, `--timeout-ms` | HTML comparison report | `analysis` |
| `analysis/compare_labels.R` | Compares benchmark configurations grouped by the `label` column within a single combined CSV (geo-mean, PAR2, NPS per label). | Single combined CSV file | Printed comparison table | `analysis` |
| `analysis/functions.R` | Shared statistical utilities (geometric mean, PAR2, NPS, CSV/JSON reader); sourced by the other R scripts. | (sourced by sibling scripts) | (library; no direct output) | `analysis` |

---

## Out of scope — test/trace infrastructure

These scripts are not benchmarking. Do not modify them as part of the benchmark
rationalisation.

| Script | What it does |
|---|---|
| `scripts/run_tests.py` | Orchestrates all three test gates (release, trace, debug) |
| `scripts/regression_runner.py` | Python harness for CTest regression tests |
| `scripts/compare_traces.py` | Compares solver search traces for divergence debugging |
| `scripts/container-build.sh` | Builds and tests the solver in a Linux container |
| `scripts/setup_remote.sh` | Sets up (clones/pulls/builds) the solver on a remote machine via SSH |
| `scripts/build-branch.sh` | Builds a clean release binary for a given branch and archives it |
| `scripts/check_output_format.py` | Checks for trailing spaces in solver output |
| `scripts/curate_test_sets.py` | Curates test sets from paper data and generates regression oracles |
| `scripts/export_test_deals.py` | Standalone re-generation of oracle JSON and deal files from curated test sets |
| `scripts/metamorphic_test.sh` | Compares flat-cache vs `--force-lru` outcomes as a metamorphic correctness check |
