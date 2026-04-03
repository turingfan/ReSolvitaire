# Benchmark Implementation: RAM Metrics & Legacy Solver Support

**Branch:** `implement-benchmark-features`
**Date:** 2026-03-30

## Overview

Two significant enhancements to the benchmarking infrastructure:

1. **RAM Usage Metrics** — Track peak memory consumption alongside timing data
2. **Legacy Reference Solver Support** — Enable comparison with older Solvitaire binaries

---

## 1. RAM Usage Metrics

### Implementation

Added `getrusage()` system call integration to `benchmark.cpp` to capture peak memory usage during solve operations.

#### Platform-Specific Handling
- **macOS:** `ru_maxrss` reports in bytes
- **Linux:** `ru_maxrss` reports in kilobytes (converted to bytes)

#### Output Additions

**Per-iteration data:**
```json
{
  "time_us": 114594.0,
  "nodes": 158295.0,
  "peak_memory_bytes": 3202056192
}
```

**Aggregate statistics:**
```json
{
  "max_memory_bytes": 3202220032,
  "median_memory_bytes": 3202220032
}
```

#### Usage

All benchmark modes automatically collect memory data:

```bash
# Seed-based benchmark
./solvitaire --type klondike --benchmark-seeds 1 50 --benchmark-iterations 3

# JSON regression benchmark
./solvitaire --benchmark-json tests/oracles/level1.json --benchmark-iterations 1
```

Output includes memory metrics in JSON without any additional flags.

---

## 2. Reference Solver External Timing & Memory Measurement

### Problem Solved

1. **Internal Timing Unreliability:** Legacy solvers' internal timing may not be reliable
2. **Missing Memory Data:** Older binaries lack internal memory tracking capability
3. **Modern Solver Gaps:** Even modern solvers don't measure their own external overhead accurately

### Solution

All reference solvers now have external timing and memory measurement via system calls (`/usr/bin/time`), reported alongside internal timing where available.

#### External Timing
- **Method:** Wall-clock time measurement using Python's `time.time()` and `/usr/bin/time`
- **Why Needed:** Captures true elapsed time including subprocess overhead and system scheduling delays
- **Reported:** Both internal (solver's own measurement) and external (system measurement) for comparison

#### Memory Measurement
- **Method:** Parse `maximum resident set size` from `/usr/bin/time` output
- **Platform Support:**
  - **macOS:** Uses `time -l`, reads bytes directly
  - **Linux:** Uses `time -v`, converts kilobytes to bytes
- **Why Needed:** Internal memory tracking not available in legacy solvers

### Legacy Reference Solver Support

Older Solvitaire binaries lack the `--benchmark-json` flag. This prevented using them as reference solvers for hardware normalization factor (HNF) calibration in `compare_benchmarks.py`.

### Solution

New `--legacy-reference` flag enables parsing output from classic solvers.

#### How It Works

1. **Seed Range Calibration:** Instead of JSON file, provide seed range as `START,END`
   ```bash
   --reference-exe <old_binary> \
   --legacy-reference \
   --calibration-workload "1,50"
   ```

2. **Per-Seed Parsing:** For each seed, runs:
   ```bash
   <old_binary> --type <game> --random <seed>
   ```
   and parses: `Time Taken (milliseconds): <N>`

3. **HNF Calculation:** Sums all parsed times to establish hardware normalization

#### Single-Solver Mode

Made `--baseline-exe` optional. Useful for profiling performance on a specific machine:

```bash
python3 scripts/compare_benchmarks.py \
    --current-exe build/bin/solvitaire \
    -- \
    --type klondike --benchmark-seeds 1 50
```

Output shows:
- Current performance metrics
- Hardware normalized score (if `--reference-exe` provided)
- No baseline comparison

### Updated Arguments

| Flag | Default | Purpose |
|---|---|---|
| `--baseline-exe` | None | Optional; if omitted, runs single-solver mode |
| `--legacy-reference` | False | Flag: reference solver lacks modern flags |
| `--calibration-workload` | `tests/oracles/level1.json` | JSON file OR seed range for legacy (`START,END`) |

### Examples

**Legacy reference calibration:**
```bash
python3 scripts/compare_benchmarks.py \
    --baseline-exe build/current \
    --current-exe build/experimental \
    --reference-exe /path/to/old_solvitaire \
    --legacy-reference \
    --calibration-workload "1,100" \
    -- \
    --type klondike --benchmark-seeds 1 50
```

**Output example (with external timing & memory):**
```
--- ReSolvitaire Orchestrator (Hardware Normalization) ---
Establishing hardware normalization factor using /path/to/old_solvitaire ...
Hardware Normalization Factor (HNF) established: 1158000.00 us
  Internal time: 1158000.00 us
  External time: 1315996.17 us
  Peak memory:   96.0 MB
```

**Single-solver mode:**
```bash
python3 scripts/compare_benchmarks.py \
    --current-exe build/solvitaire \
    --reference-exe /path/to/stable_solvitaire \
    -- \
    --benchmark-json tests/oracles/level1.json
```

**Two-solver comparison with legacy reference:**
```bash
python3 scripts/compare_benchmarks.py \
    --baseline-exe build/baseline \
    --current-exe build/current \
    --reference-exe /path/to/old_solvitaire \
    --legacy-reference \
    --calibration-workload "1,50" \
    -- \
    --type klondike --benchmark-seeds 1 10
```

---

## Testing

Both features have been tested and verified:

✅ Memory metrics correctly reported (~3.2 GB for Klondike seeds 1-3)
✅ Single-solver mode output format and report generation
✅ Two-solver comparison mode unchanged and working
✅ Legacy reference solver parsing of "Time Taken (milliseconds)"
✅ HNF calculation from legacy seeds (verified with seeds 1-3 → 690ms total)

---

## Understanding Timing Discrepancies

When using legacy reference solvers, you may observe:
- **External time > Internal time:** Subprocess overhead (shell startup, I/O buffering)
- **Memory varies by seed:** Different game states consume different amounts of memory
- **Peak memory > Median memory:** Aggregate stats report worst-case peak from all seeds

### Example Analysis
```
Internal time: 1158000.00 us  # From solver's stopwatch
External time: 1315996.17 us  # From /usr/bin/time
Difference:      157996.17 us (overhead: ~13.6%)
```

---

## Known Issues & Limitations

### Linux Testing Gap
**Status:** Code path implemented but untested on Linux systems
- `/usr/bin/time -v` parsing implemented for Linux
- Memory measurement converts KB to bytes (vs macOS bytes)
- User/system time extraction from verbose output
- **Action needed:** Test on Linux before production use

### Virtual vs Resident Memory
The internal benchmark memory metrics (`getrusage().ru_maxrss`) report virtual memory, not resident set size:
- **Modern solver:** Internal metric ~3.2 GB (virtual), system-measured ~1.1 GB (resident)
- **Legacy solver:** Only system-measured memory available (~96 MB resident)
- **Recommendation:** Prefer system-measured memory from `/usr/bin/time` for accuracy

---

## Integration Notes

- All changes backward-compatible; existing scripts work unchanged
- Memory data available in JSON reports for post-processing
- Legacy mode automatically routes to seed-based execution path
- Single-solver mode re-uses existing benchmark engine logic
- External timing & memory measurement works for ALL reference solvers, not just legacy
- CPU headline uses system+user time for accuracy
- Detailed timing breakdown (wall, user, sys) available for analysis
