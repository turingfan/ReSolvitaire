# ReSolvitaire Benchmarking Framework

This document explains how to use the ReSolvitaire Benchmarking Framework. The framework consists of a native C++ benchmarking engine for microsecond-precision timing and a Python Orchestrator script for hardware-agnostic normalization and comparative reporting.

---

## Overview

The benchmarking infrastructure is designed to:
1. Provide highly accurate microsecond timings across a sequence of random game seeds.
2. Measure a "Standard Candle," which is a hardcoded simple workload used to establish the absolute speed of your current hardware.
3. Automatically normalize all execution times against this Standard Candle, allowing for direct comparisons between runs on entirely different hardware (e.g., a 2019 Intel Mac vs. a Linux server).
4. Export rich, machine-readable JSON reports containing detailed statistical math (`mean`, `median`, `sd`, `min`, `max`) and environmental metadata (`Git Hash`, `Machine ID`, `Date`). The engine now uses **SAX-style streaming JSON** for near-zero memory overhead even on massive runs.
5. Priority is given to **median-based statistics** for hardware normalization and speedup calculations, as they are significantly more resilient to background OS noise.

---

## 1. C++ Benchmarking Engine

The C++ executable has been extended with native flags to run the engine. Instead of outputting standard solver logs, it runs a timing loop over the requested seeds and emits a JSON payload to `stdout`.

### Command Line Flags

- `--benchmark`: Triggers the benchmark engine mode.
- `--benchmark-seeds <start> <end>`: Specifies the inclusive range of seeds to benchmark.
- `--benchmark-iterations <N>`: The number of times to solve each seed (default: `1`).
- `--benchmark-json <path>`: Runs a benchmark across multiple instances defined in a JSON file. This is the preferred way to run regression benchmarks (Levels 1-5). The engine automatically resolves file paths and handles both Array and Object JSON formats.

### Example

```bash
./solvitaire --type klondike --benchmark-seeds 1 100 --benchmark-iterations 3 --benchmark-warmup 1
```

For regression benchmarks:
```bash
./solvitaire --benchmark-json tests/oracles/level1.json --benchmark-iterations 1
```

This will run every instance in the Level 1 JSON, streaming results for each one. The engine performs intelligent path resolution, searching for deal files in:
1. The exact path provided in the JSON (`instance` or `instance_name` keys).
2. `tests/` relative to the current directory.
3. `tests/resources/` relative to the current directory.
4. Stripped versions of the paths (e.g., removing `instances/` prefix common in Level 1/3 files).

---

## 2. Python Orchestrator (`compare_benchmarks.py`)

The python script acts as an orchestrator. It automatically measures the Hardware Normalization Factor (HNF) on your machine using a Reference Solver, then runs the requested benchmark workload on both a `baseline` executable and your `current` executable, finally comparing the two.

### Usage

```bash
python3 scripts/compare_benchmarks.py \
    --baseline-exe <path_to_baseline_binary> \
    --current-exe <path_to_current_binary> \
    -- \
    <benchmark_args...>
```

- `--baseline-exe`: Path to the control or `master` build.
- `--current-exe`: Path to the experimental or current working build.
- `--out-report`: (Optional) The output JSON filename. Defaults to `benchmark_report.json`.
- `--reference-exe <path>`: (Recommended) The path to the stable reference binary. If omitted, HNF defaults to `1.0` and results are not hardware-normalized.
- `--calibration-workload <path>`: (Optional) Path to the JSON file used for calibration. Defaults to `tests/oracles/level1.json` (multi-second baseline).
- `--`: Separates orchestrator arguments from the arguments that will be forwarded to the C++ benchmark engine.

### Example

```bash
python3 scripts/compare_benchmarks.py \
    --baseline-exe build/bin/solvitaire_baseline \
    --current-exe build/bin/solvitaire \
    -- \
    --type klondike --benchmark-seeds 1 50 --benchmark-iterations 2
```

### Output

The script outputs a markdown-style terminal report mimicking the following format:

```text
================ BENCHMARK REPORT ================

Date:         2026-03-21T17:31:13.422789
Machine ID:   MCWD704HG9LV
Git Hash:     82699f827c308f16478c2584a16607a35474fb07
Workload:     --type klondike --benchmark-seeds 1 50 --benchmark-iterations 2

--- Timing (Median us) ---
Baseline: 98788.00 (Mean: 99123.00, SD: 93153.00)
Current:  97978.00 (Mean: 98456.00, SD: 92567.00)

--- Node Statistics ---
Baseline: 81047.50 (Mean: 81047.50)
Current:  81047.50 (Mean: 81047.50)
Baseline NPS:   820418.47 nodes/sec
Current NPS:    827201.00 nodes/sec

--- Hardware Normalized ---
Standard Candle (Median):    251542.96 us
Baseline Normalized Score:   0.3927
Current Normalized Score:    0.3895

--- Comparison (Median-Based) ---
Time Ratio (Current/Baseline): 0.9918x
Node Ratio (Current/Baseline): 1.0000x
Verdict: Current build is FASTER by 0.82%
```

A comprehensive `benchmark_report.json` file is also created, making this suite exceptionally easy to hook into CI pipelines.

---

## 3. Hardware Normalization (Reference Solver)

Establishing a machine-independent performance baseline is critical for comparing results across different hardware.

### Methodology

The orchestrator establishes a **Hardware Normalization Factor (HNF)** by running a specific, stable version of Solvitaire (the **Reference Solver**) on a deterministic set of problems (the **Calibration Workload**). 

The raw execution time of any benchmark run is then divided by this HNF to produce a dimensionless **Normalized Score**. A lower score indicates better performance.

### Usage

1.  **Obtain a Reference Binary**: Compile a stable version of Solvitaire (e.g., from a specific git tag like `v0.10.2`) in Release mode. For maximum portability across different OS versions, consider static linking.
2.  **Run Calibration**: Use the `--reference-exe` flag in the orchestrator.

```bash
python3 scripts/compare_benchmarks.py \
    --baseline-exe build/bin/solvitaire_baseline \
    --current-exe build/bin/solvitaire \
    --reference-exe /path/to/stable_reference_solvitaire \
    -- \
    --benchmark-json tests/oracles/level1.json
```

### Command Line Flags

- `--reference-exe <path>`: (Recommended) The path to the stable reference binary. If omitted, HNF defaults to `1.0` and results are not hardware-normalized.
- `--calibration-workload <path>`: (Optional) Path to the JSON file used for calibration. Defaults to `tests/oracles/level1.json` which provides a robust multi-second baseline (~2 seconds on modern hardware).

---

## Output Examples

### Aggregate Mode (Seeds)

```text
--- Hardware Normalized ---
Reference Solver HNF:        251542.96 us
Baseline Normalized Score:   0.3927
Current Normalized Score:    0.3895

--- Comparison (Median-Based) ---
Time Ratio (Current/Baseline): 0.9918x
Verdict: Current build is FASTER by 0.82%
```

### Paired Mode (JSON)

```text
================ PAIRED INSTANCE BENCHMARK ================

Workload: --benchmark-json tests/oracles/level1.json
Total instances matched: 150

--- Hardware Normalized ---
Reference Solver HNF:        251542.96 us
Baseline Median Ratio:       0.9918x (Relative to reference HNF if provided)

--- Median Ratios (Across all instances) ---
Median Time Ratio: 0.9918x
Median Node Ratio: 1.0000x

Verdict: Current build is FASTER by 0.82%
```

A comprehensive `benchmark_report.json` file is also created, making this suite exceptionally easy to hook into CI pipelines.

---

## Known Issues / Future Work

- **Suit-Symmetry Games Not Benchmarked With Flat Cache:** When `--streamliners suit-symmetry`
  or `--streamliners both` is active, the solver automatically falls back to the LRU cache
  (see `docs/known-issues.md` issue #3 and #4). This means any benchmark run on Spanish Patience
  or other suit-symmetry games measures LRU performance, not flat cache performance. Flat-vs-LRU
  comparisons are therefore incomplete until suit-canonical hashing is implemented in the flat cache.
  This is the most important gap to address in benchmarking work on a future branch.
- **Boost Dependency:** The C++ executable currently relies on `boost/program_options` for parsing command-line arguments. This introduces a heavy external dependency. Future work should consider migrating to a lightweight, header-only argument parser (like `cxxopts` or `argparse`) to reduce build times and simplify cross-platform compilation of the Reference Solver.
- **Python Memory Overhead:** The `compare_benchmarks.py` orchestrator currently pipes the C++ engine's `stdout` to a temporary file on disk, but then reads the entire file back into memory (`output = tmp.read()`) to find the valid JSON chunk. This introduces unneeded disk I/O while still holding the full string in memory. Future iterations can parse `subprocess.PIPE` iteratively or use a streaming JSON parser in Python to genuinely save RAM.
