# Getting Started: Benchmarking & Analysis

This guide covers two workflows: running benchmarks locally, and running them on a
remote compute server from your local machine.

---

## Local Workflow

### 1. Build

```bash
./build.sh --release
```

This builds the default `solvitaire` binary. To also build variant binaries:

```bash
cmake --build cmake-build-release --target solvitaire-flat
cmake --build cmake-build-release --target solvitaire-hash-only
cmake --build cmake-build-release --target solvitaire-lru
```

### 2. Run benchmarks

**Quick validation** (5 games, 50 seeds each):
```bash
python3 scripts/benchmark_orchestrator.py \
    --solver cmake-build-release/bin/solvitaire \
    --workers 4 --quick \
    --output-dir results/$(date +%Y%m%d)_quick
```

**Full run** (all game types, 200 seeds each):
```bash
python3 scripts/benchmark_orchestrator.py \
    --solver cmake-build-release/bin/solvitaire \
    --workers $(sysctl -n hw.logicalcpu 2>/dev/null || nproc) \
    --output-dir results/$(date +%Y%m%d)
```

### 3. Analyse results

```bash
# Summary of a single run
Rscript analysis/summary.R results/20260407/combined.csv

# Compare cache configurations by label
Rscript analysis/compare_labels.R results/20260407/combined.csv

# Baseline vs. current comparison
Rscript analysis/benchmark.R \
    --baseline results/baseline.csv \
    --current  results/20260407/combined.csv
```

---

## Remote Workflow (SSH from local machine)

All remote scripts are run **from your local machine**. They SSH into the remote
and do the work there; you never need to log in manually.

### 1. Set up the remote (first time)

```bash
bash scripts/setup_remote.sh \
    --host user@server \
    --repo https://github.com/turingfan/ReSolvitaire.git
```

This clones the `dev` branch, installs dependencies, and builds all four variant
binaries. It is idempotent — safe to rerun to pull the latest and rebuild:

```bash
bash scripts/setup_remote.sh --host user@server
```

Options: `--branch BRANCH`, `--commit SHA`, `--dir PATH` (remote working directory,
default `~/ReSolvitaire-caching`).

### 2. Start a benchmark run

SSH in to kick off the orchestrator (or use `nohup`/`tmux` for long runs):

```bash
ssh user@server "cd ~/ReSolvitaire-caching && nohup python3 scripts/benchmark_orchestrator.py \
    --solver cmake-build-release/bin/solvitaire \
    --workers \$(nproc) \
    --output-dir results/\$(date +%Y%m%d) \
    > results/run.log 2>&1 &"
```

Or for a quick validation before committing to the full run:

```bash
ssh user@server "cd ~/ReSolvitaire-caching && python3 scripts/benchmark_orchestrator.py \
    --solver cmake-build-release/bin/solvitaire \
    --workers \$(nproc) --quick \
    --output-dir results/\$(date +%Y%m%d)_quick"
```

### 3. Collect results

Once the run is complete, fetch the results to your local machine:

```bash
bash scripts/collect_results.sh --host user@server
```

This tars up `results/` on the remote and scp's it to `~/Downloads/`. Options:

- `--remote-dir results/20260407` — collect a specific subdirectory
- `--local-dir ~/benchmarks` — save to a different local path
- `--remote-root ~/ReSolvitaire-caching` — if you used a non-default `--dir`

### 4. Analyse locally

```bash
tar xzf ~/Downloads/benchmark_server_*.tar.gz -C /tmp/
Rscript analysis/summary.R /tmp/results/combined.csv
Rscript analysis/compare_labels.R /tmp/results/combined.csv
```

---

## Benchmark Orchestrator Options

| Flag | Default | Description |
|---|---|---|
| `--solver` | (required) | Path to solvitaire binary |
| `--workers` | CPU count | Parallel worker processes |
| `--output-dir` | `results/remote` | Where to write CSV/JSON output |
| `--quick` | off | Small subset (5 games, 50 seeds) for validation |
| `--configs` | `auto hash-only force-lru` | Cache configurations to compare |

Pass arbitrary solver flags after `--`:
```bash
python3 scripts/benchmark_orchestrator.py --solver ... -- --cache-capacity 4294967296
```

---

## Metrics (compare_labels.R)

| Metric | Meaning |
|---|---|
| Time Geo-Mean | Central tendency of wall-clock time (resistant to outliers) |
| PAR2 Score | Timeouts penalised at 2× the timeout limit |
| Aggregate NPS | Total nodes / total time — global throughput |
| Result Diffs | Instances where solution outcome differed between labels |
| Speedup | Ratio relative to baseline label (1.5× = 50% faster) |
