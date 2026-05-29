# Benchmarking START HERE

**Read this first.** It tells you which script to run for which job.
For the CSV column contract and `solution_type` vocabulary, see
[csv_schema.md](csv_schema.md).
For the full rationalisation plan, see
`01-Knowledge-Base/Implementation-Plans/benchmark-rationalisation-plan-2026-05-29.md`.

---

> **Stage 2 updates (2026-05-29)**
>
> - **Worker-count safety (T2/T3, now fixed):** `benchmark_orchestrator.py`
>   defaults workers to a **memory-aware cap** (floor(total_RAM × 80% / 3 GB))
>   and drives jobs via **GNU parallel** (`--jobs N --memfree 3G`).  The old
>   `multiprocessing.cpu_count()` default is gone.  Print the computed cap and
>   the limiting factor before any run starts.  A warning is printed if
>   `--workers` exceeds the safe ceiling (but the run is not refused).
> - **Chunk timeout (T2, now fixed):** each `run_benchmark.py` chunk has a
>   Python-side hard ceiling.  A wedged chunk is SIGTERM'd → SIGKILL'd and
>   recorded as failed; the pool continues.
> - **Full-matrix guard rail (T5):** the 14-game × 500-seed GAME_CONFIGS_FULL
>   matrix requires `--full` to opt in.  The default scope is GAME_CONFIGS_QUICK
>   (5 games, 50 seeds, 30 s).
> - **SIGTERM / kill (T1, already fixed):** process-group kill is the standard
>   path — solver + `/usr/bin/time` share a session; `os.killpg` reaches both.
>   Partial stdout is always captured and classified.
> - **`compare_benchmarks.py` vs R analysis layer:** `compare_benchmarks.py`
>   was removed (deprecated).  Use the R layer (`analysis/summary.R`,
>   `analysis/benchmark.R`, `analysis/compare_labels.R`) as the canonical path.

---

## 1. One-binary single run

Run the solver on a seed range for one game type; write a CSV.

```bash
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike \
    --seeds 1-150 \
    --timeout 60000 \
    --output results/klondike_current.csv
```

A quick R summary is printed automatically if R is available. Common options:

| Option | Notes |
|---|---|
| `--iterations N` | Timed runs per instance (default 1) |
| `--warmup N` | Warmup runs excluded from CSV (default 0) |
| `--streamliner X` | `none`, `auto-foundations`, `suit-symmetry`, `both`, `smart-solvability` |
| `--cache-capacity N` | Cache capacity in bytes (omit to use solver default) |
| `--label TEXT` | Freeform tag written to the `label` CSV column |
| `--append` | Append to existing CSV (use with `--no-header`) |
| `--legacy` | Use `--classify` output for pre-ReSolvitaire binaries |

See [quickstart.md](quickstart.md) for more examples (file-based instances, custom
rules, oracle-driven runs, legacy comparison).

---

## 2. Parallel multi-game / multi-binary run

**Prerequisites:** [GNU parallel](https://www.gnu.org/software/parallel/) must
be on `PATH`.  Install it with `brew install parallel` (macOS),
`apt-get install parallel` (Debian/Ubuntu), or `yum install parallel`
(RHEL/CentOS).  `--dry-run` works without `parallel` installed.

Fan out across multiple game types and solver binaries in parallel (via GNU
parallel), producing `combined.csv` for the `ReSolvitaire-bench` hook.

```bash
# Default scope: GAME_CONFIGS_QUICK (5 games, 50 seeds, 30 s)
# Workers: automatically memory-bounded (see output for computed cap)
python3 scripts/benchmark_orchestrator.py \
    --solver-dir cmake-build-release/bin \
    --output-dir results/$(date +%Y%m%d)
```

The script prints the computed worker count and limiting factor before running.
Workers default to `min(cpu_count//2, floor(total_RAM × 80% / 3 GB))`.
Use `--workers N` to override (a warning is printed if N exceeds the safe cap,
but the run is not refused).

**Opt-in to full matrix (`--full`):** the full `GAME_CONFIGS_FULL` matrix
(14 games × 500 seeds × 20-min timeout) requires `--full`:

```bash
python3 scripts/benchmark_orchestrator.py \
    --solver-dir cmake-build-release/bin --full \
    --output-dir results/$(date +%Y%m%d)
```

**Dry run** (no binaries or `parallel` needed):

```bash
python3 scripts/benchmark_orchestrator.py --dry-run \
    --solver-dir cmake-build-release/bin \
    --games free-cell klondike --seeds 1-10
```

Quick scope override:

```bash
# Quick smoke test: 2 games, 10 seeds, auto workers
python3 scripts/benchmark_orchestrator.py \
    --solver-dir cmake-build-release/bin \
    --games free-cell klondike \
    --seeds 1-10 \
    --timeout 30000 \
    --output-dir /tmp/smoke
```

---

## 3. Oracle-driven runs

Run the solver on all instances matching a regression oracle, accumulating results
into a single CSV.

```bash
# Benchmark all unsolvable Level 5 instances with the flat binary
python3 scripts/oracle_to_benchmark_cmds.py \
    --oracle tests/oracles/level5.json \
    --solution-type unsolvable \
    --solver cmake-build-release/bin/solvitaire-flat \
    --warmup 1 --iterations 3 --timeout 1800000 \
    --skip-ineligible --no-summary \
    --output results/level5_flat.csv | bash
```

`oracle_to_benchmark_cmds.py` reads a regression oracle JSON and emits one
`run_benchmark.py` shell command per matching entry. The first command writes the
CSV header; subsequent commands use `--no-header --append`. Pipe the output to
`bash` to execute. Pass solver-specific flags after `--`:

```bash
python3 scripts/oracle_to_benchmark_cmds.py \
    --oracle tests/oracles/level5.json \
    --solution-type unsolvable \
    --solver cmake-build-release/bin/solvitaire-lru \
    --warmup 1 --iterations 3 --timeout 1800000 \
    --output results/level5_lru.csv -- --force-lru | bash
```

---

## 4. Bespoke experiment example

For a canned multi-phase experiment comparing solver variants, use the
`bench_multiplicity.sh` script as the exemplar pattern:

```bash
# Dry run — prints commands, runs nothing
./scripts/experiments/bench_multiplicity.sh --dry-run --phase D --seeds 1-10

# Smoke test (5 seeds, 1 game, 1 worker)
./scripts/experiments/bench_multiplicity.sh --phase D --seeds 1-5 \
    --games klondike --workers 1

# Quick validation (50 seeds, all phases, 4 workers)
./scripts/experiments/bench_multiplicity.sh --phase ABCD --seeds 1-50 \
    --workers 4
```

This script shells out to `run_benchmark.py` (the correct pattern for bespoke
experiments). See `scripts/experiments/` for other canned experiment scripts.

---

## 5. Analysis

### Quick summary (single CSV)

```bash
Rscript analysis/summary.R results/klondike_current.csv
```

Prints solve rate, geometric mean time, PAR2 score, and NPS. Also called
automatically by `run_benchmark.py` at the end of a run.

### Full comparison report (baseline vs current)

```bash
# Produce baseline
python3 scripts/run_benchmark.py \
    --solver builds/reference/solvitaire \
    --type klondike --seeds 1-150 --timeout 60000 \
    --output results/baseline.csv

# Run current
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-150 --timeout 60000 \
    --output results/current.csv

# Generate HTML report
Rscript analysis/benchmark.R \
    --baseline results/baseline.csv \
    --current  results/current.csv \
    --output   results/comparison.html
open results/comparison.html
```

### Compare runs by label (single combined CSV)

When multiple configurations are accumulated into one CSV via `--label` and
`--append`, compare them with:

```bash
Rscript analysis/compare_labels.R results/combined.csv
```

The R layer (`analysis/summary.R`, `analysis/benchmark.R`, `analysis/compare_labels.R`,
`analysis/functions.R`) is the **canonical analysis path**. Do not write new ad-hoc
Python CSV parsers — use these scripts.

---

## 6. Archiving via ReSolvitaire-bench

Results are archived in the `ReSolvitaire-bench` repo using the `bench` CLI (not in
this solver repo). The recommended flow is:

```bash
# Run experiment through bench (auto-archives CSV and commits to DataLad)
bench --detached --hook benchmark -m "Experiment description" <run-name> \
    -- ./scripts/experiments/bench_multiplicity.sh --phase D --seeds 1-100

# After the run, lodge results
bench-lodge
```

For details on `bench`, `bench-lodge`, and `bench-import`, see the README in the
`ReSolvitaire-bench` repo. Do not duplicate that documentation here.

The `benchmark` hook in `ReSolvitaire-bench` reads `data/combined.csv` (produced by
`benchmark_orchestrator.py`) and counts `solution_type` values. The vocabulary contract
is in [csv_schema.md](csv_schema.md) — any change to it must keep the hook working.
