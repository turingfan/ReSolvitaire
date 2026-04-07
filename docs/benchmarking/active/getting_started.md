# Getting Started: Remote Benchmarking & Analysis

This guide covers the automated workflow for setting up a remote compute instance, running parallelized benchmarks, and performing comparative statistical analysis.

## 1. Remote Environment Setup

The `setup_remote.sh` script prepares a fresh Linux or macOS machine for benchmarking by installing dependencies (CMake, Boost, R, Python) and building the solver in Release mode.

```bash
# Clone and build for the first time
bash scripts/setup_remote.sh --repo https://github.com/turingfan/ReSolvitaire.git --branch dev

# Or just update and rebuild an existing directory
bash scripts/setup_remote.sh --dir ~/ReSolvitaire-caching
```

## 2. Orchestrating Parallel Benchmarks

Instead of running long sequences of seeds manually, use `benchmark_orchestrator.py`. This script splits the workload into parallel chunks, taking full advantage of many-core CPUs.

### Basic Invocations
- **Quick Validation**: Runs a subset of 5 games (50 seeds each) to check system performance.
  ```bash
  python3 scripts/benchmark_orchestrator.py --solver cmake-build-release/bin/solvitaire --quick
  ```
- **Full Run**: Executes the full production suite across all supported game types.
  ```bash
  python3 scripts/benchmark_orchestrator.py --solver cmake-build-release/bin/solvitaire --workers 32
  ```

### Advanced Controls
- **Compare Caches**: Automatically runs benchmarks for multiple configurations (e.g., `auto`, `hash-only`, and `force-lru`).
  ```bash
  python3 scripts/benchmark_orchestrator.py --solver [PATH] --configs auto hash-only
  ```
- **Custom Passthrough**: Pass any arbitrary solver flags (like capacity) using the `--` separator.
  ```bash
  python3 scripts/benchmark_orchestrator.py --solver [PATH] -- --cache-capacity 4294967296
  ```

## 3. Results Collection

Once completed, the orchestrator produces a `combined.csv` file in the `--output-dir` (default: `results/remote`). Use the helper script to bundle these for transfer:

```bash
bash scripts/collect_results.sh results/remote
# Then run the 'scp' command printed by the script on your local machine.
```

## 4. Statistical Analysis

Comparative analysis is handled by `analysis/compare_labels.R`. It groups results by the `--label` provided during benchmarking and calculates key metrics.

```bash
# Compare all labels in the dataset relative to 'auto'
Rscript analysis/compare_labels.R results/combined.csv
```

### Metrics Explained
- **Time Geo-Mean**: Central tendency of wall-clock time (highly resistant to outliers).
- **PAR2 Score**: Penalizes timeouts/unsolvable instances at 2x the timeout limit.
- **Aggregate NPS**: Total nodes searched / total time (global throughput).
- **Result Diffs**: Count of instances where solution outcomes mismatched between labels.
- **Node-Reduction / Speedup**: Ratios relative to the baseline (e.g., 1.5x means 50% faster).
