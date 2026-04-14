# TASK: Implement Phase 1 of Legacy Benchmarking Plan

You are an AI developer tasked with updating the Python benchmarking infrastructure for ReSolvitaire. You will work on the `benchmark-python` branch.

## Preparation
1. Ensure you are on the `benchmark-python` branch. Run `git checkout benchmark-python` and `git pull origin benchmark-python`.
2. Read the following documentation for context:
   - `docs/benchmarking/options/legacy_comparison_status.md`
   - `docs/benchmarking/options/legacy_implementation_plan.md` (Focus on Phase 1)

## Objective
Implement a "handshake" mechanism in the Python scripts to automatically detect if a `solvitaire` binary is Modern, Semi-Legacy, or True Legacy, and enforce correct behavioral rules for each.

## Requirements

### 1. Unified Handshake in `scripts/run_benchmark.py`
- Create a function `interrogate_binary(solver_path: str) -> str`.
- It must run `solver_path --benchmark-handshake`.
- If the output contains `modern v` (e.g. `modern v1.0`), return `"MODERN"`.
- If the output contains `semi-legacy v` (e.g. `semi-legacy v0.1`), return `"SEMI_LEGACY"`.
- If the command fails (exit code != 0), return `"TRUE_LEGACY"`.

### 2. Guardrails & Enforcement
In `run_benchmark.py`:
- Use the handshake result to configure the run.
- **MODERN**: Use `--json` as normal.
- **SEMI_LEGACY**: Switch to CSV parsing. Expect the header `[BENCHMARK_CSV_START]`. Capture the appended `solver_resident_bytes` at the end of the CSV line. Modern flags (like `--cache-capacity`) ARE allowed.
- **TRUE_LEGACY**: 
  - If the user did NOT pass `--legacy`, ABORT with an error message.
  - If the user DID pass `--legacy`, proceed with the old regex-based CSV parser.
  - **CRITICAL**: Enforce a strict whitelist of arguments for True Legacy. If any argument NOT in the whitelist (e.g. `--label`) is provided, the script must FAIL explicitly with an error message. Do not silently strip the flag.

### 3. Update modern solver (handshake only)
- Make a minimal change to the modern solver code (likely `src/main/main.cpp` or the argument parser) so that `--benchmark-handshake` returns `modern v1.0` and exits 0.

## Completion
1. Verify the changes by attempting to run `run_benchmark.py` against the current binary. It should now detect it as "MODERN".
2. Commit your changes with a descriptive message.
3. Push the branch to `origin benchmark-python`.
