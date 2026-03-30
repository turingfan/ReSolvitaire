# Benchmarking Enhancements Walkthrough

We have modernized the ReSolvitaire benchmarking system to provide more robust statistical insights and preserve detailed run data.

## Key Changes

### 1. Advanced Statistical Metrics
The benchmarking engine now calculates and reports more reliable metrics:
- **Geometric Mean**: Used as the primary metric for time and nodes to better represent performance across skewed datasets.
- **PAR2 Scoring**: Penalizes timeouts by treating them as 2x the timeout duration, ensuring that failures are properly reflected in the performance score.
- **Multi-Layered NPS**: Reports both per-instance NPS (Mean/Median) and global Aggregate NPS (Total Nodes / Total Time).

### 2. Detailed Logging
Added a new `--save-details` flag to `scripts/compare_benchmarks.py`.
- **Preservation**: All raw seed-level data is now preserved in a dedicated JSON file (e.g., `details.json`).
- **Transparency**: Users can now inspect individual run results even when using aggregate modes.

### 3. Orchestrator Improvements
- Updated `scripts/compare_benchmarks.py` to use Geometric Mean as the primary comparison factor.
- Standardized NPS reporting to show both granular and aggregate views.
- Updated `scripts/benchmark_speedup.sh` to leverage these new metrics for cache strategy comparisons.

## Verification Results

### C++ Engine Output
Verified that the JSON output from `solvitaire --benchmark` now includes the expanded statistics:
```json
"aggregate_stats": {
    "geometric_mean_time_us": 15936.1672,
    "par2_score_us": 137657.6,
    "mean_nps": 1233933.65,
    "aggregate_nps": 1609606.73,
    ...
}
```

### Orchestrator Comparison
Ran a full comparison using the new `compare_benchmarks.py` logic:
```
--- Comparison (Current / Baseline) ---
Time Ratio (Geo-Mean): 0.8022x
PAR2 Score Ratio:      0.8446x
Node Ratio (Median):   1.0000x
Nodes/Sec Ratio (Mean): 1.1875x

Verdict: Current build is FASTER by 1.25x
Saving full benchmark details to details.json
```

### Raw Data Preservation
Confirmed that `details.json` contains full seed arrays:
```json
"baseline": {
    "seed_data": {
        "1": [{"time_us": 264880.0, "nodes": 158295.0, ...}],
        ...
    }
}
```

## How to Use
To run a comparison with detailed logging:
```bash
python3 scripts/compare_benchmarks.py --current-exe ./bin/solvitaire --baseline-exe ./bin/old_solvitaire --save-details benchmark_details.json -- --type klondike --benchmark-seeds 1 100
```
