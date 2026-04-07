# DevLog: Scalable Remote Benchmarking Infrastructure Rewrite
**Date: 2026-04-07**

## Context
The previous benchmarking flow on the `dev` branch was inefficient, manually intensive, and disconnected from the core `run_benchmark.py` script. The goal was to build a cohesive, automated remote execution pipeline utilizing `multiprocessing` and standardized analysis scripts.

## Milestones Achieved

### 1. `run_benchmark.py` Flexibility
- **Passthrough Support**: Added `--` support allowing arbitrary arguments (`--cache-type`, `--force-lru`) to be passed directly to the `solvitaire` binary. This removed the need to update the benchmark script every time a new solver flag is added.
- **Labeling Mechanism**: Added a `--label` flag to tag individual segments of a benchmark run. This enables downstream analysis to reliably compare configurations (e.g. `auto` vs `hash-only`) when datasets are merged.

### 2. High-Throughput Orchestration
- **`benchmark_orchestrator.py`**: A new high-level wrapper that replaces the custom sequential loops on `dev`.
- **Seed Chunking**: It partitions the total bench-seed workload into chunks (e.g., 5–10 seeds per chunk), which are then dispatched to `run_benchmark.py` across all available CPU worker threads. This maximizes throughput on large compute instances while maintaining fresh solver invocations for every seed.
- **Merge logic**: Automatically aggregates multiple temporary `.csv` and `.json` logs from parallel workers into a unified `combined.csv` dataset.

### 3. Remote Lifecycle Automation
- **`setup_remote.sh`**: Developed an idempotent Bash wrapper for preparing remote environments. It clones the desired branch, installs dependencies (CMake, Boost, R), builds the binaries, and provides copy-paste ready commands for execution.
- **`collect_results.sh`**: A utility for bundling results into a timestamped archive and printing the `scp` command for local retrieval.

### 4. Advanced Comparative Analysis
- **`compare_labels.R`**: A new statistical tool that replaces the hardcoded `compare_caches.R`. It compares labeled runs within a combined dataset.
- **Consistency**: Integrated metrics from `summary.R`, including **Geometric Mean** (time/nodes), **PAR2 Score**, and **Aggregate NPS**.
- **Outcome Auditing**: Added a **Result Differences** audit that flags instances where the winnability outcome (SOLVED/TIMEOUT/UNWINNABLE) differed between two configurations on the same seed.

## Implementation Notes
- The final state of the benchmarking tools was successfully migrated to both the `dev` branch (for general use) and restored to a standalone state on `benchmark-python` (for ongoing benchmark-specific development).
- The `dev` branch was historical-reset to remove a messy first iteration of these scripts before applying the clean versions.
