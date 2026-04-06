# Implementation Plan: Benchmarking Orchestrator Enhancements

This plan addresses two limitations in `compare_benchmarks.py`:
1.  **Requirement of two solvers**: Enable running a single solver for performance profiling and normalization.
2.  **Hardware Normalization with Legacy Solvers**: Enable calibration using "classic" Solvitaire binaries that do not support modern JSON benchmark flags.

## Proposed Changes

### [Component] Benchmarking Scripts

#### [MODIFY] [compare_benchmarks.py](file:///Users/ipg/Research/ReSolvitaire-project/02-Code-Repositories/ReSolvitaire-Benchmarking/scripts/compare_benchmarks.py)

1.  **Argument Parsing**:
    -   Change `--baseline-exe` from `required=True` to `default=None`.
    -   Add `--legacy-reference` (bool flag) to indicate the reference binary is a classic solver.
    -   Update `--calibration-workload` to support a simple comma-separated list of seeds (e.g., `1,2,3,4,5`) if a JSON file isn't suitable.
2.  **Legacy Calibration Logic**:
    -   In `measure_standard_candle`, if `--legacy-reference` is set:
        -   Instead of `--benchmark-json`, run the solver once for each seed/instance using `--type klondike --random <seed>`.
        -   Capture the `stdout` and use a regex to parse the `Time Taken (milliseconds): <N>` line.
        -   Sum these times to calculate the HNF.
3.  **Single-Solver Execution Path**:
    -   In `main`, if `--baseline-exe` is `None`:
        -   Skip baseline execution.
        -   Run only the `current-exe`.
        -   Update the report and text output to show only the "Current" performance metrics and its "Hardware Normalized Score".
        -   The "Verdict" and "Comparison" sections will be omitted.

## Verification Plan

### Automated Tests
- Test single-solver execution:
  `python3 scripts/compare_benchmarks.py --current-exe bin/mac/solvitaire-mac-arm64 --reference-exe bin/mac/solvitaire-mac-arm64 -- --type klondike --benchmark-seeds 1 5`
- Test legacy calibration (verified through manual inspection of the logic and a mock output test if possible).

### Manual Verification
- Verify the printed report correctly omits comparison columns when only one solver is used.
- Verify the Normalized Score is correctly calculated and displayed.
