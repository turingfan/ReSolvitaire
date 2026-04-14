# Legacy Solvitaire Benchmarking: Status & Options

This document outlines the current status and necessary steps for using the `benchmark-python` infrastructure to compare the current codebase against two historical versions of Solvitaire.

---

## Option 1: Unmodified Historical Solvitaire (True Legacy)

This involves compiling and running a historical snapshot of Solvitaire *without making any code changes*. We must rely strictly on its existing command-line interfaces (e.g., `--classify`) and augment it externally (e.g., via `/usr/bin/time` for memory profiling).

### (A) Can this be run easily now with `benchmark-python` as is?
**Yes, partially.** `run_benchmark.py` already includes a `--legacy` flag. When used, it:
1. Replaces the `--json` argument with `--classify`.
2. Uses the `parse_legacy_classify()` function instead of JSON parsing.
3. Automatically attempts to handle both the 13-column (standard) and 24-column (smart/streamliner) CSV outputs that the old solver produced.
4. Captures memory usage (Resident Set Size) externally via `/usr/bin/time` (captured as `resident_memory_bytes`), compensating for the lack of internal memory reporting.

However, it is fragile regarding exact output formatting, particularly if the historical codebase prints unexpected logging before the CSV line.

### (B) Adaptations to Scripts Necessary to Improve Status
To robustly support Option 1, the following adaptations to Python scripts are needed:

1. **Robust Output Parsing:** `parse_legacy_classify` currently assumes `_CSV_LINE_RE` will find the right line. If historical versions interspersed error logs or debug info, this might fail. We need to ensure the parser gracefully skips non-CSV stdout lines and strictly validates column counts.
2. **Missing Metric Handling:** The legacy `--classify` output does not provide `solver_resident_bytes` (internal memory tracking). The scripts currently default it to `0`. We need to ensure downstream analysis (like `summary.R` or `compare_benchmarks.py`) handles `0` values gracefully (e.g., showing `N/A` instead of skewing averages) when comparing legacy vs. modern runs.
3. **Smart Streamliner Edge Cases:** If running with `--streamliners smart-solvability`, the old code outputs two passes on one line. The Python script extracts the second-pass stats if they exist. We need dedicated test fixtures verifying this parser logic against historical output examples.
4. **Command-Line Compatibility:** Older versions might not accept certain modern flags (e.g., `--cache-capacity` may be silently ignored or cause crashes). The orchestrator needs a strict argument whitelist when running in `--legacy` mode.
5. **Explicit Version Override:** Add a `--commit-override` or firmly utilize the existing `--label` flag in `run_benchmark.py`. Currently, the Python script uses `git rev-parse HEAD` to populate the `solver_commit` column. If we run a pre-compiled legacy binary while our terminal is checked out to `dev`, the results will incorrectly attribute the run to the `dev` commit. We need to manually inject the true legacy commit hash for reliable version tracking.

### (C) Minimal Changes to Solvitaire Required
*None.* By definition, Option 1 requires the codebase to remain untouched. We compensate entirely in python.

---

## Option 2: Minimally Modified Legacy Solvitaire (Semi-Legacy)

This involves taking a historical snapshot of Solvitaire and updating it *only* with the absolute minimum changes needed to interface smoothly with modern benchmarking scripts. **Crucially, this does not require backporting JSON output.**

### (A) Can this be run easily now with `benchmark-python` as is?
**Yes.** Using the `--legacy` flag, `run_benchmark.py` expects CSV output. If the semi-legacy binary maintains the historical CSV/SSV format, it works out of the box. The Python tools are already designed to handle `--classify` CSV output.

### (B) Adaptations to Scripts Necessary to Improve Status
To make this perfectly seamless, the scripts need:
1. **Header Awareness:** Update Python's legacy parser to look for a specific header line (e.g., `[BENCHMARK_CSV_START]`) so it never accidentally parses debug output.
2. **Flag Passthrough:** Keep using `--legacy` in the orchestrator, and allow it to pass modern flags (like `--cache-capacity`) with the expectation that the semi-legacy binary will ignore them safely.
3. **Version Passing Configuration:** Similar to Option 1, ensure the orchestrator accepts explicit labels/commit overrides, since we will likely be compiling this "semi-legacy" binary from a detached tag or custom branch.

### (C) Minimal Changes to Solvitaire Required to Improve Reporting
To achieve this highly decoupled Option 2, the following surgical changes must be backported to the legacy codebase:

1. **Graceful Argument Ignoring:** Update `command_line_helper` (via Boost Program Options) to allow unregistered options (`allow_unregistered()`). This ensures flags like `--label` or `--cache-capacity` don't cause fatal parse errors.
2. **Reliable CSV Output Keys/Headers:** Prepend a strict header marker right before the CSV output prints, making Python parsing robust regardless of other debug text.
3. **Internal Memory Output (Optional but recommended):** Backport the `get_peak_rss()` query and append the memory footprint byte count to the end of the CSV row so Python can track internal memory explicitly.

*Note: By relying on robust CSV parsing rather than JSON, we dramatically reduce the number of C++ files that must be modified back in the legacy timeline.*
