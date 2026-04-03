# Benchmarking Quickstart

Run your first benchmark in 5 minutes.

## Prerequisites

- Built solver: `./build.sh` → `cmake-build-release/bin/solvitaire`
- Python 3.8+
- R with `ggplot2` and `rmarkdown` (for full reports; optional for basic summary)

## 1. Single run (sanity check)

```bash
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike \
    --seeds 1-10 \
    --timeout 10000 \
    --output results/test.csv
```

Output: `results/test.csv` with 10 rows (one per seed). A quick R summary is
printed to stdout automatically if R is available.

## 2. Standard Level 1 benchmark (150 seeds)

```bash
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike \
    --seeds 1-150 \
    --timeout 60000 \
    --output results/klondike_current.csv
```

Takes ~2 minutes. Summary printed on completion.

## 3. Compare two builds

```bash
# Baseline (e.g. reference binary or previous commit)
python3 scripts/run_benchmark.py \
    --solver builds/reference/solvitaire \
    --type klondike --seeds 1-150 --timeout 60000 \
    --output results/baseline.csv

# Current
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-150 --timeout 60000 \
    --output results/current.csv

# Full comparison report
Rscript analysis/benchmark.R \
    --baseline results/baseline.csv \
    --current  results/current.csv \
    --output   results/comparison.html
open results/comparison.html
```

## 4. File-based instances (Level 1 JSON files)

```bash
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --instances "tests/resources/level1/klondike/*.json" \
    --timeout 60000 \
    --output results/level1.csv
```

## 5. Inspect the CSV

```bash
head -5 results/klondike_current.csv
```

Columns: `instance, seed, run, solution_type, time_us, nodes, ...` — see
[csv_schema.md](csv_schema.md) for the full reference.

## 6. Compare current vs legacy solver

```bash
# Legacy binary (uses --classify flag, no --json support)
python3 scripts/run_benchmark.py \
    --solver /path/to/old/solvitaire \
    --type klondike --seeds 1-150 --timeout 60000 \
    --legacy --output results/klondike_legacy.csv

# Full comparison
Rscript analysis/benchmark.R \
    --baseline results/klondike_legacy.csv \
    --current  results/klondike_current.csv \
    --output   results/legacy_vs_current.html
```

Note: `solver_resident_bytes` is 0 in legacy runs (the old solver has no
`--json` output). `resident_memory_bytes` (from `/usr/bin/time`) is still
captured accurately. All 20 CSV columns are populated.

## 7. Manual R summary

```bash
Rscript analysis/summary.R results/klondike_current.csv
```

## Common options

| Option | Default | Description |
|---|---|---|
| `--iterations N` | 1 | Timed runs per instance |
| `--warmup N` | 0 | Warmup runs (excluded from output) |
| `--streamliner X` | `none` | `none`, `auto-foundations`, `suit-symmetry`, `both`, `smart-solvability` |
| `--cache-capacity N` | 100000000 | Cache entry limit |
| `--legacy` | off | Use `--classify` output (for pre-ReSolvitaire solver binaries) |
| `--output-json FILE` | off | Also write JSON output |
| `--no-summary` | off | Skip automatic R summary at end |
| `--no-header` | off | Suppress CSV column headers |
