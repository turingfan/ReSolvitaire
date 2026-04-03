# Benchmarking Framework Design

**Branch:** `benchmark-python`
**Status:** Implementation complete
**Last revised:** 2026-04-03

---

## Contents

1. [Goals and Principles](#1-goals-and-principles)
2. [Architecture Overview](#2-architecture-overview)
3. [Data Flow](#3-data-flow)
4. [C++ Layer](#4-c-layer)
5. [Python Orchestration (`run_benchmark.py`)](#5-python-orchestration-run_benchmarkpy)
6. [R Analysis Scripts](#6-r-analysis-scripts)
7. [Comparison Workflow](#7-comparison-workflow)
8. [CSV Schema](#8-csv-schema)
9. [Documentation Strategy](#9-documentation-strategy)
10. [Directory Structure](#10-directory-structure)
11. [Implementation Checklist](#11-implementation-checklist)
12. [HDF5 (Deferred)](#12-hdf5-deferred)

---

## 1. Goals and Principles

**Goals:**
- Run solver experiments reproducibly on seed ranges or file-based instances
- Compare two solver variants (baseline vs current) with rigorous statistics
- Produce human-readable CSV output and publication-ready R reports
- Keep benchmarking code isolated from core solver to avoid merge contamination

**Principles:**

1. **No C++ orchestration.** Seed loops, iteration counts, warmup, and timing
   are Python's job. C++ runs one instance and exits.

2. **No statistics in C++.** Geometric mean, PAR2, NPS, percentiles — all
   computed in Python (quick CLI summary) and R (rigorous, publication-quality).

3. **CSV is primary output.** Column headers always present by default. One row
   per run (raw, not aggregated). Aggregation is R's job.

4. **JSON is an option, not the default.** The solver's existing `--json` flag
   is how Python reads per-run output from C++. Python can also write JSON with
   `--output-json` for downstream compatibility.

5. **Orchestration includes R summary.** After collecting all runs, `run_benchmark.py`
   automatically invokes `analysis/summary.R` (if R is available) to print a
   concise statistical summary to stdout. Full comparison reports are produced
   separately via `analysis/benchmark.R`.

6. **Memory tracking uses `/usr/bin/time`.** Python wraps each solver invocation
   with `/usr/bin/time -l` (macOS) or `/usr/bin/time -v` (Linux) to capture
   per-run peak RSS from stderr. This gives accurate, per-run isolation.
   C++ also self-reports RSS in its `--json` output as a secondary diagnostic
   (`solver_resident_bytes`), which is 0 for legacy solver runs.

7. **HDF5 deferred.** Not needed at current scale; revisit above ~100k runs.

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│  C++ (solvitaire)                                   │
│  One instance per invocation.                       │
│  --json outputs per-run stats to stdout.            │
│  --benchmark / --solvability modes retained.        │
└───────────────────────┬─────────────────────────────┘
                        │ subprocess + JSON stdout
┌───────────────────────▼─────────────────────────────┐
│  Python (scripts/run_benchmark.py)                  │
│  • Drives seed loop or file list                    │
│  • Warmup, iterations, timeout                      │
│  • Measures wall-clock time and RSS                 │
│  • Writes CSV (default) and/or JSON (optional)      │
│  • On completion: calls analysis/summary.R          │
│    (if R available) → prints quick stats to stdout  │
└───────────────────────┬─────────────────────────────┘
                        │ CSV / JSON files
┌───────────────────────▼─────────────────────────────┐
│  R (analysis/benchmark.R)                           │
│  • Reads CSV or JSON (either format)                │
│  • Geometric mean, PAR2, Wilcoxon tests, CIs        │
│  • Per-instance scatter plots (ggplot2)             │
│  • Exports HTML report                              │
└─────────────────────────────────────────────────────┘
```

---

## 3. Data Flow

```
solvitaire --json --type klondike --random 42 --timeout 60000
    stdout → { "nodes": ..., "solution_type": ..., ... }

run_benchmark.py
    for each (seed, run_index):
        t0 = time.perf_counter()
        result = subprocess.run([/usr/bin/time -l/-v, solver, "--json", ...])
        t1 = time.perf_counter()
        rss = parse /usr/bin/time stderr output  (per-run peak RSS, in bytes)
        parse result JSON (or --classify CSV for --legacy mode)
        append CSV row
    on completion:
        Rscript analysis/summary.R results.csv   → stdout summary

analysis/benchmark.R --baseline baseline.csv --current current.csv --output report.html
    → report.html  (full statistical comparison)
```

---

## 4. C++ Layer

### What is retained unchanged

- `--json` flag: single-run output to stdout. This is the primary Python interface.
- `--benchmark` mode: legacy multi-seed loop with aggregated JSON. Retained for
  backward compatibility.
- `--solvability` mode: retained.

### New addition (cherry-picked from `mac-dev-benchmark-enhancements`)

- `resident_memory_bytes` field added to `--json` output via `getrusage(RUSAGE_SELF)`.
  Reported as `solver_resident_bytes` in CSV for diagnostic comparison against
  Python-measured RSS. No new CLI flags required.

### Reverted

- `--benchmark-seeds`, `--benchmark-iterations`, `--benchmark-warmup`,
  `--benchmark-json` — these delegated orchestration to C++ and are not ported.
  Python handles all of this.

---

## 5. Python Orchestration (`run_benchmark.py`)

### Command-line interface

```bash
# Seed-based run
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike \
    --seeds 1-150 \
    --streamliner none \
    --cache-capacity 100000000 \
    --timeout 60000 \
    --iterations 1 \
    --warmup 1 \
    --output results/klondike_current.csv \
    [--output-json results/klondike_current.json] \
    [--no-header]              # suppress CSV header (rarely needed)
    [--no-summary]             # skip automatic R summary at end

# File-based run
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --instances "tests/resources/level1/klondike/*.json" \
    --timeout 60000 \
    --output results/level1_current.csv

# Legacy solver (pre-ReSolvitaire binary, uses --classify instead of --json)
python3 scripts/run_benchmark.py \
    --solver /path/to/old/solvitaire \
    --type klondike \
    --seeds 1-150 \
    --timeout 60000 \
    --legacy \
    --output results/klondike_legacy.csv
```

### Behaviour

- Calls solver once per `(instance, run_index)` pair, using `--json` (default)
  or `--classify` with `--legacy`.
- Warmup runs execute but are not written to output.
- CSV column headers written on first row by default.
- Rows flushed to disk incrementally — partial results preserved on interruption.
- `solver_commit` captured from `git rev-parse --short HEAD` at startup.
- Wall-clock time measured in Python around each subprocess call.
- Peak RSS measured per-run via `/usr/bin/time -l` (macOS) or `/usr/bin/time -v`
  (Linux) wrapped around the solver subprocess. Falls back to 0 if unavailable.
- On completion: invokes `Rscript analysis/summary.R <output.csv>` if `Rscript`
  is on PATH. Prints summary table to stdout. Skipped gracefully if R not found.

### Automatic R summary

The summary script (`analysis/summary.R`) prints a concise table:

```
=== Benchmark Summary: klondike_current.csv ===
Instances:     150
Solved:        127 (84.7%)
Unwinnable:     20 (13.3%)
Timeout:         3 (2.0%)

Timing (us):
  Geometric mean:   1 423.7
  Median:           1 210.4
  PAR2 score:       3 891.2

Nodes:
  Geometric mean:    8 432
  Median:            7 109
  Aggregate NPS:   5 924 183

Memory (resident, MB):
  Median:           312.4
  Max:              401.1
```

This runs automatically at the end of every `run_benchmark.py` invocation unless
`--no-summary` is passed.

---

## 6. R Analysis Scripts

### `analysis/functions.R`

Statistical utilities used by all other R scripts:

- `geometric_mean(x)` — handles zeros and timeouts
- `par2_score(times, timeout_ms)` — PAR2 with configurable penalty
- `wilcoxon_paired(baseline, current)` — Wilcoxon signed-rank test with CI
- `speedup_summary(baseline, current)` — geometric mean speedup with bootstrap CI
- `read_benchmark(path)` — reads CSV or JSON (detected by extension)

### `analysis/summary.R`

Called automatically by `run_benchmark.py` at the end of a run. Prints a
concise summary table to stdout. Accepts one argument: path to CSV or JSON file.

```bash
Rscript analysis/summary.R results/klondike_current.csv
```

### `analysis/benchmark.R`

Full comparison between two result sets. Accepts `--baseline`, `--current`,
`--output` (HTML report path), and `--metric` (default: `time_us`).

```bash
Rscript analysis/benchmark.R \
    --baseline results/klondike_baseline.csv \
    --current  results/klondike_current.csv \
    --output   results/comparison_report.html
```

HTML report includes:
- Summary table (geometric mean, PAR2, median, solve rates)
- Per-instance scatter plot (baseline vs current time)
- Wilcoxon signed-rank test with p-value
- Geometric mean speedup with bootstrap 95% CI
- Solution type agreement table

Both scripts read CSV or JSON interchangeably.

---

## 7. Comparison Workflow

### Full comparison (replaces `compare_benchmarks.py`)

```bash
# Step 1: Run baseline (e.g. reference binary)
python3 scripts/run_benchmark.py \
    --solver builds/reference/solvitaire \
    --type klondike --seeds 1-150 --timeout 60000 \
    --output results/baseline.csv

# Step 2: Run current
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-150 --timeout 60000 \
    --output results/current.csv

# Step 3: Full statistical comparison
Rscript analysis/benchmark.R \
    --baseline results/baseline.csv \
    --current  results/current.csv \
    --output   results/comparison.html
```

The automatic R summary at the end of each `run_benchmark.py` call gives a
quick sanity check without waiting for the full comparison.

### `compare_benchmarks.py`

The existing script is **deprecated**. It is not deleted immediately — a notice
at the top points to the new workflow. It will be removed in a subsequent branch
cleanup.

---

## 8. CSV Schema

One row per run. Column headers always present by default.

| Column | Type | Description |
|---|---|---|
| `instance` | string | `klondike_42` (seed-based) or basename (file-based) |
| `seed` | int or blank | Random seed; blank for file-based instances |
| `run` | int | Run index, 1-based, excluding warmup |
| `solution_type` | string | `SOLVED`, `UNWINNABLE`, `TIMEOUT` |
| `time_us` | float | Wall-clock time in microseconds (Python-measured) |
| `nodes` | int | States searched |
| `unique_nodes` | int | Unique states (from cache occupied count) |
| `backtracks` | int | Backtrack count |
| `dominance_moves` | int | Dominance moves applied |
| `states_removed_from_cache` | int | Cache evictions |
| `cache_size` | int | Final cache occupied entries |
| `cache_buckets` | int | Total cache bucket count |
| `max_depth` | int | Maximum search depth reached |
| `final_depth` | int | Final search depth |
| `resident_memory_bytes` | int | Per-run peak RSS via `/usr/bin/time` (primary; bytes) |
| `solver_resident_bytes` | int | RSS from C++ `getrusage` in `--json` output (diagnostic; 0 for legacy) |
| `streamliner` | string | Streamliner setting (e.g. `none`, `auto-foundations`) |
| `cache_capacity` | int | `--cache-capacity` value |
| `timeout_ms` | int | Timeout used |
| `solver_commit` | string | 7-char git hash of the solver binary's commit |

Full column descriptions with notes: see [csv_schema.md](csv_schema.md).

---

## 9. Documentation Strategy

### In-repo docs (`docs/benchmarking/`)

The detailed technical documentation lives in the repository alongside the
code. It is version-controlled and updated with the branch.

| Document | Audience | Content |
|---|---|---|
| `README.md` | All | Folder index |
| `active/design.md` | Developer | Architecture, data flow, full design (this file) |
| `active/quickstart.md` | User | Run a benchmark in 5 minutes |
| `active/csv_schema.md` | Developer / analyst | Every CSV column, types, caveats |
| `active/r_analysis.md` | Analyst | R scripts: usage, extending, adding metrics |

### Knowledge-Base docs (`01-Knowledge-Base/Design-Documents/`)

High-level decisions and proposals that should survive branch deletion. The KB
doc `benchmarking_framework_proposal.md` summarises the architecture and points
to the in-repo docs for detail. It is not a duplicate — it records *why*
decisions were made (e.g. why HDF5 was deferred, why C++ orchestration was
removed).

### Archive (`docs/benchmarking/archive/`)

Superseded docs from the `mac-dev-benchmark-enhancements` era. Retained for
historical context. Not updated.

### Results (`docs/benchmarking/results/`)

Reference result files (e.g. sample CSV from a baseline run). Gitignored for
generated output; only curated reference files are committed here.

---

## 10. Directory Structure

```
scripts/
    run_benchmark.py        # Python orchestration — main entry point
    compare_benchmarks.py   # DEPRECATED — see design.md §7
    extract_benchmark_results.py  # utility (retained from mac-dev)

analysis/
    functions.R             # shared statistical utilities
    summary.R               # quick single-file summary (called by run_benchmark.py)
    benchmark.R             # full two-way comparison report

docs/benchmarking/
    README.md               # this folder's index
    active/
        design.md           # this file — full architecture and design
        quickstart.md       # 5-minute user guide
        csv_schema.md       # column reference
        r_analysis.md       # R scripts guide
    archive/                # superseded docs from mac-dev-benchmark-enhancements
    results/                # reference result files (.gitignore generated output)

results/                    # gitignored — runtime output from run_benchmark.py
    *.csv
    *.json
    *.html
```

---

## 11. Implementation Checklist

### Branch setup
- [ ] Create `benchmark-python` from `dev`
- [ ] Cherry-pick `get_resident_memory_bytes()` C++ addition from `mac-dev-benchmark-enhancements`
- [ ] Cherry-pick `extract_benchmark_results.py` from `mac-dev`
- [ ] Add `results/` to `.gitignore`
- [ ] Add deprecation notice to `compare_benchmarks.py`

### C++
- [ ] Verify `resident_memory_bytes` present in `--json` single-run output
- [ ] Verify no `--benchmark-seeds` / orchestration flags present

### Python (`scripts/run_benchmark.py`)
- [ ] Seed loop (`--seeds N-M`)
- [ ] File list (`--instances glob`)
- [ ] Warmup (excluded from output)
- [ ] Iterations
- [ ] Wall-clock timing (Python)
- [ ] RSS measurement via `resource` module
- [ ] CSV output with headers (default)
- [ ] JSON output (`--output-json`)
- [ ] `--no-header` flag
- [ ] Incremental CSV flush (row-by-row)
- [ ] `solver_commit` from `git rev-parse --short HEAD`
- [ ] Auto-invoke `Rscript analysis/summary.R` on completion
- [ ] Graceful skip if R not available
- [ ] `--no-summary` flag

### R scripts
- [ ] `analysis/functions.R` — geometric mean, PAR2, Wilcoxon, speedup CI, `read_benchmark()`
- [ ] `analysis/summary.R` — single-file quick summary to stdout
- [ ] `analysis/benchmark.R` — two-file comparison, HTML report

### Documentation
- [ ] `docs/benchmarking/active/quickstart.md`
- [ ] `docs/benchmarking/active/csv_schema.md`
- [ ] `docs/benchmarking/active/r_analysis.md`
- [ ] Update `CLAUDE.md` with benchmark workflow
- [ ] Update KB proposal to point to in-repo docs

---

## 12. HDF5 (Deferred)

The original proposal described HDF5 as primary storage. At current scale
(150–10,000 runs), CSV is adequate (~1–15 MB uncompressed). The case for HDF5:

- Structured metadata embedded in the file (hardware, config, commit)
- ~100× smaller than JSON for large experiments
- Native chunked access — no need to load the whole file

Revisit when experiments regularly exceed ~100k runs or when embedded metadata
becomes important. The Python and R layers are designed so adding HDF5 output
is additive (write HDF5 alongside CSV; R reads either).
