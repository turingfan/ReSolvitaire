# ReSolvitaire Remote Benchmarking Infrastructure

This directory contains scripts and tools designed to facilitate large-scale, automated benchmarking of the ReSolvitaire solver on remote compute instances.

## 🚀 Quick Start (Remote Execution)

1.  **Setup**: Run `setup_remote.sh` to prepare a fresh compute instance (clones the repo, installs dependencies like R/CMake/Boost, and builds the solver).
    ```bash
    bash scripts/setup_remote.sh --repo <github-url> --branch dev
    ```
2.  **Benchmark**: Use the `benchmark_orchestrator.py` to run parallel tests. It automatically handles seed chunking and worker allocation.
    ```bash
    # Run a quick validation (5 games, 50 seeds each)
    python3 scripts/benchmark_orchestrator.py --solver cmake-build-release/bin/solvitaire --workers 32 --quick
    
    # Run a full production benchmark
    python3 scripts/benchmark_orchestrator.py --solver cmake-build-release/bin/solvitaire --workers 32 --output-dir results/$(date +%Y%m%d)
    ```
3.  **Collect**: Bundle and retrieve your results using `collect_results.sh`.
    ```bash
    bash scripts/collect_results.sh results/my_run
    ```

## 📊 Analysis Tools

- **`analysis/compare_labels.R`**: The primary tool for comparing different solver configurations (e.g., `auto` vs `hash-only`).
    - Outputs **Geometric Mean** for time and nodes.
    - Calculates **PAR2 scores** and **Aggregate NPS**.
    - Reports **Result Differences** between configurations on matched seeds.
    ```bash
    Rscript analysis/compare_labels.R results/my_run/combined.csv
    ```

## 🛠 File Overview

- **`scripts/run_benchmark.py`**: The core single-threaded benchmark runner. Supports `--label` for tagging runs and `--` for passing arbitrary flags to the solver.
- **`scripts/benchmark_orchestrator.py`**: High-level wrapper that manages parallel clusters of `run_benchmark.py` invocations.
- **`scripts/setup_remote.sh`**: Idempotent setup script for remote environments.
- **`scripts/collect_results.sh`**: Helper for results retrieval.
- **`analysis/compare_labels.R`**: Statistical comparison engine across labeled runs.
