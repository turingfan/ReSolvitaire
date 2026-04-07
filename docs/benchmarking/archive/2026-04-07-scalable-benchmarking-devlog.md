# DevLog: Scalable Remote Benchmarking Infrastructure Rewrite
**Date: 2026-04-07**

## Overview
Replaced the previous inefficient benchmarking flow with a cohesive, automated remote execution pipeline on both `dev` and a standalone `benchmark-python` branch.

### 🏁 Core Achievement: Orchestrated Parallelism
- **Problem**: Running thousands of seeds took dozens of hours and was manually error-prone.
- **Solution**: Developed `benchmark_orchestrator.py`, a high-throughput wrapper that dynamically fragments seed ranges into chunks for multi-worker Python processing while maintaining solver instance isolation.

### ⚙️ Automation Strategy
1.  **Idempotent Setup**: Created `setup_remote.sh` for one-click environment preparation.
2.  **Flexible Passthrough**: Implemented the `--` (REMAINDER) passthrough in `run_benchmark.py`, enabling benchmarking of arbitrary solver flags (e.g., `--cache-type hash-only`) without code modification.
3.  **Labeling & Comparison**: Added a mandatory labeling mechanism to tag benchmark permutations, feeding into a redesigned `compare_labels.R` statistical engine.

### 📊 Comparative Analysis Engine
- Re-architected the analysis script to provide **Geometric Mean** and **PAR2** metrics (consistent with `summary.R`).
- Implemented a **Conflict Audit** that flags instances where differing cache configurations yielded different win/loss/timeout outcomes on the same seed.
- Added **Aggregate NPS** and **Throughput Reducer** metrics to clearly quantify speed gains (e.g., *“hash-only provides 14% NPS improvement”*).

### 🛠 Git Hygiene
- Sanitized the `dev` branch history by resetting it back to the stable `ea66cec` base, discarding the initial experimental benchmarking commits while preserving the clean final migration.
- Restored `benchmark-python` as a standalone specialized branch, divorced from the main implementation history but sharing the new toolset.
