# Benchmarking START HERE

**Read this first.** It tells you which script to run for which job.
For the CSV column contract and `solution_type` vocabulary, see
[csv_schema.md](csv_schema.md).
For the full rationalisation plan, see
`01-Knowledge-Base/Implementation-Plans/benchmark-rationalisation-plan-2026-05-29.md`.

---

> **Known rough edges (being fixed in Stage 2)**
>
> - **Worker-count safety:** `benchmark_orchestrator.py` defaults workers to
>   `multiprocessing.cpu_count()`. On many-core machines that can spawn hundreds
>   of processes (each worker forks Python → `/usr/bin/time` → solver). Do NOT
>   set `--workers` to a large value without budgeting RAM: each concurrent solver
>   holds a flat-cache mmap (~several GB). This has crashed machines. Use a
>   conservative value (e.g. `--workers 4`) until Stage 2 adds memory-aware
>   concurrency.
> - **SIGTERM / kill caveat:** `run_benchmark.py`'s safety-valve SIGTERM is sent
>   to the immediate child process only (the `/usr/bin/time` wrapper), not the
>   whole process group. The solver grandchild can be orphaned. Runs that hit the
>   safety valve appear as `TERMINATED` or `KILLED` in the CSV, not as clean
>   `TIMEOUT`. The goal is that every hard instance ends as `TIMEOUT`. Stage 2
>   will fix the process-group signalling.
> - **`compare_benchmarks.py` vs R analysis layer:** the role and continued use of
>   `scripts/compare_benchmarks.py` relative to `analysis/benchmark.R` is under
>   evaluation in Stage 2. Until that is resolved, use the R layer (documented
>   below) as the canonical analysis path.

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

Fan out across multiple game types and solver binaries in parallel; produce
`combined.csv` for the `ReSolvitaire-bench` hook.

```bash
python3 scripts/benchmark_orchestrator.py \
    --solver-dir cmake-build-release/bin \
    --workers 4 \
    --output-dir results/$(date +%Y%m%d)
```

This discovers `solvitaire`, `solvitaire-flat`, `solvitaire-hash-only`,
`solvitaire-lru` in `--solver-dir` and runs all game configurations in
`GAME_CONFIGS_FULL` (14 games, 500 seeds, 20-minute timeout per instance).
The final merged file is `<output-dir>/combined.csv`.

**Warning — see the "Known rough edges" box above before setting `--workers`.**
The full `GAME_CONFIGS_FULL` matrix is very long; use `--games` and `--seeds` to
scope down for a quick test:

```bash
# Quick smoke test: 2 games, 10 seeds, 2 workers
python3 scripts/benchmark_orchestrator.py \
    --solver-dir cmake-build-release/bin \
    --games free-cell klondike \
    --seeds 1-10 \
    --timeout 30000 \
    --workers 2 \
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
