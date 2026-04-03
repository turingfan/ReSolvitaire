# Implementation Brief: `benchmark-python` Branch

**For:** Haiku agent
**Reviewed by:** Opus/Sonnet after completion — see evaluation-checklist.md
**Design docs:** docs/benchmarking/active/design.md (read this first)

---

## Overview

Create branch `benchmark-python` from `dev` and implement the Python/R
benchmarking framework described in design.md. This involves:

1. Branch setup and cherry-picks
2. Small C++ enhancement to `--json` output
3. Python orchestration script (`scripts/run_benchmark.py`)
4. Three R scripts (`analysis/functions.R`, `analysis/summary.R`, `analysis/benchmark.R`)
5. Housekeeping (`.gitignore`, deprecation notice, `CLAUDE.md`)

**Do not modify any other C++ files.** Do not touch the solver logic, CMake,
CI, or regression infrastructure.

---

## Step 1: Create Branch

```bash
git checkout dev
git checkout -b benchmark-python
```

---

## Step 2: Cherry-pick `extract_benchmark_results.py`

```bash
git cherry-pick 00147b78e42b0298569cc8e175d7a57e61bc014e
```

This adds `scripts/extract_benchmark_results.py`. Verify the file exists after.
If the cherry-pick has conflicts, resolve by keeping the incoming file as-is.

---

## Step 3: Enhance `--json` single-run output in C++

**File:** `src/main/main.cpp`

The `--json` flag currently outputs only 5 fields. We need to add more.
Find the block that starts with `if (clh.get_json_output()) {` in the
`solve_game` function (around line 205). It currently writes:

```
instance_name, solution_type, states_searched, unique_states, backtracks, max_depth
```

Extend it to also write (in this order, after `max_depth`):

```cpp
writer.Key("dominance_moves");
writer.Uint64(s.second.dominance_moves);
writer.Key("states_removed_from_cache");
writer.Uint64(s.second.states_removed_from_cache);
writer.Key("cache_size");
writer.Uint64(s.second.cache_size);
writer.Key("cache_buckets");
writer.Uint64(s.second.cache_bucket_count);
writer.Key("final_depth");
writer.Uint64(s.second.depth);
```

Then add resident memory at the end:

```cpp
// Memory measurement (getrusage RUSAGE_SELF)
{
    struct rusage usage;
    uint64_t rss_bytes = 0;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef __APPLE__
        rss_bytes = (uint64_t)usage.ru_maxrss;  // bytes on macOS
#else
        rss_bytes = (uint64_t)usage.ru_maxrss * 1024ULL;  // KB on Linux
#endif
    }
    writer.Key("solver_resident_bytes");
    writer.Uint64(rss_bytes);
}
```

Ensure `#include <sys/resource.h>` is at the top of main.cpp. Check if it
is already present before adding.

Verify `solver::result` has fields `dominance_moves`, `states_removed_from_cache`,
`cache_size`, `cache_bucket_count`, `depth` by checking `src/main/solver/solver.h`.
Use whatever field names exist — do not rename fields in solver.h.

**Build check:**
```bash
./build.sh --release
./cmake-build-release/bin/solvitaire --type klondike --random 1 --json
```
The output must be valid JSON containing all new fields.

---

## Step 4: Python script — `scripts/run_benchmark.py`

Create this file. It must be executable (`chmod +x`).

### Command-line interface

```
usage: run_benchmark.py [-h]
    --solver PATH
    (--seeds N-M | --instances GLOB [GLOB ...])
    [--type TYPE]
    [--streamliner {none,auto-foundations,suit-symmetry,both,smart}]
    [--cache-capacity N]
    [--timeout N]
    [--iterations N]
    [--warmup N]
    --output FILE.csv
    [--output-json FILE.json]
    [--no-header]
    [--no-summary]
```

### Behaviour

**Seed mode** (`--seeds N-M`): iterate seed from N to M inclusive.
Requires `--type`.

**Instance mode** (`--instances GLOB`): glob-expand each pattern, iterate
over matching files sorted. `--type` not used (instance file provides rules).

For each instance, run `--iterations` timed runs, preceded by `--warmup`
warmup runs (excluded from output). Default: iterations=1, warmup=0.

**Per-run subprocess call:**

```python
cmd = [solver_path, "--json", "--timeout", str(timeout_ms)]
# seed mode:
cmd += ["--type", game_type, "--random", str(seed)]
# instance mode:
cmd += [instance_path]
# optional:
if streamliner != "none":
    cmd += ["--streamliners", streamliner]
if cache_capacity is not None:
    cmd += ["--cache-capacity", str(cache_capacity)]
```

Wrap call with `time.perf_counter()` before and after `subprocess.run()`.
Time in microseconds = `(t1 - t0) * 1_000_000`.

After each call measure RSS:
```python
import resource
rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
# Normalise: Linux reports KB, macOS reports bytes
import sys, platform
if platform.system() == "Darwin":
    rss_bytes = rss          # already bytes
else:
    rss_bytes = rss * 1024   # convert KB → bytes
```

Also capture virtual memory:
```python
# ru_maxrss is peak RSS; for virtual, use ru_ixrss on some platforms.
# If unavailable or zero, report 0.
vms = resource.getrusage(resource.RUSAGE_CHILDREN).ru_ixrss
vms_bytes = vms * 1024 if platform.system() != "Darwin" else vms
```

**Parse solver JSON output** from stdout. Expected fields (all optional —
default to 0/empty if missing to be robust against old binaries):
`solution_type`, `states_searched`, `unique_states`, `backtracks`,
`max_depth`, `dominance_moves`, `states_removed_from_cache`,
`cache_size`, `cache_buckets`, `final_depth`, `solver_resident_bytes`.

Map `solution_type` values: `"winnable"` → `"SOLVED"`,
`"unsolvable"` → `"UNWINNABLE"`, `"timeout"` → `"TIMEOUT"`,
anything else → `"UNKNOWN"`.

**CSV output:**

Columns in this exact order:
```
instance, seed, run, solution_type, time_us, nodes, unique_nodes,
backtracks, dominance_moves, states_removed_from_cache,
cache_size, cache_buckets, max_depth, final_depth,
resident_memory_bytes, virtual_memory_bytes, solver_resident_bytes,
streamliner, cache_capacity, timeout_ms, solver_commit
```

- Write header row on first row unless `--no-header`.
- Open CSV file for writing at start, flush after every data row.
- Warmup rows are NOT written.
- `instance`: seed mode → `{type}_{seed}` (e.g. `klondike_42`);
  instance mode → basename without extension.
- `seed`: the integer seed, or empty string for instance mode.
- `run`: 1-based index of timed runs (1, 2, ... iterations).
- `solver_commit`: capture once at startup:
  ```python
  import subprocess
  result = subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                          capture_output=True, text=True)
  commit = result.stdout.strip() if result.returncode == 0 else "unknown"
  ```

**JSON output** (if `--output-json` given): write a JSON array of objects,
one per run, with the same fields as the CSV (field names identical to
column names). Write after all runs complete.

**Automatic R summary** (unless `--no-summary`): after all runs, call:
```python
import shutil
if shutil.which("Rscript"):
    subprocess.run(["Rscript", "analysis/summary.R", output_csv_path])
```
If Rscript not found, print: `"(R not available — skipping summary. Install R to enable.)"`.
This must not fail or raise an exception if R is absent.

**Progress:** print one line per instance to stderr:
```
[  1/150] klondike_1    SOLVED      1423.7 us   8432 nodes
[  2/150] klondike_2    UNWINNABLE   892.1 us   4219 nodes
```

**Error handling:** if solver exits non-zero, print a warning to stderr and
write a row with `solution_type=ERROR` and all numeric fields 0.

---

## Step 5: R scripts

Create the `analysis/` directory. Create three files.

### `analysis/functions.R`

```r
# Shared statistical utilities for ReSolvitaire benchmarking

library(jsonlite)

# Read CSV or JSON benchmark file
read_benchmark <- function(path) {
  if (grepl("\\.json$", path, ignore.case = TRUE)) {
    df <- as.data.frame(fromJSON(path))
  } else {
    df <- read.csv(path, stringsAsFactors = FALSE)
  }
  df
}

# Geometric mean (handles zeros and NA)
geometric_mean <- function(x, na.rm = TRUE) {
  x <- x[!is.na(x)]
  if (length(x) == 0 || any(x <= 0)) return(NA_real_)
  exp(mean(log(x)))
}

# PAR2 score: timeout/unwinnable penalised at 2 * timeout_ms * 1000 us
par2_score <- function(times_us, solution_types, timeout_ms) {
  penalty <- 2 * timeout_ms * 1000
  adjusted <- ifelse(solution_types %in% c("TIMEOUT", "UNWINNABLE", "ERROR"),
                     penalty, times_us)
  mean(adjusted, na.rm = TRUE)
}

# Aggregate NPS: total nodes / total time in seconds
aggregate_nps <- function(nodes, times_us) {
  total_time_s <- sum(times_us, na.rm = TRUE) / 1e6
  if (total_time_s == 0) return(NA_real_)
  sum(nodes, na.rm = TRUE) / total_time_s
}

# Paired Wilcoxon signed-rank test on matched instances
# baseline and current are data frames with columns: instance, time_us
wilcoxon_paired <- function(baseline, current) {
  merged <- merge(baseline[, c("instance", "time_us")],
                  current[, c("instance", "time_us")],
                  by = "instance", suffixes = c("_b", "_c"))
  if (nrow(merged) < 5) return(list(p_value = NA, estimate = NA))
  test <- wilcox.test(merged$time_us_b, merged$time_us_c,
                      paired = TRUE, conf.int = TRUE)
  list(p_value = test$p.value, estimate = test$estimate)
}

# Bootstrap CI for geometric mean speedup (baseline / current)
speedup_ci <- function(baseline_times, current_times, R = 1000) {
  ratios <- baseline_times / current_times
  ratios <- ratios[is.finite(ratios) & ratios > 0]
  if (length(ratios) < 2) return(list(estimate = NA, lower = NA, upper = NA))
  b <- boot::boot(ratios, function(x, i) geometric_mean(x[i]), R = R)
  ci <- tryCatch(boot::boot.ci(b, type = "perc")$percent[4:5],
                 error = function(e) c(NA, NA))
  list(estimate = geometric_mean(ratios), lower = ci[1], upper = ci[2])
}
```

### `analysis/summary.R`

```r
#!/usr/bin/env Rscript
# Quick summary of a single benchmark result file.
# Called automatically by run_benchmark.py; also usable manually.
# Usage: Rscript analysis/summary.R <results.csv|results.json>

source(file.path(dirname(sys.frame(1)$ofile), "functions.R"), chdir = TRUE)

args <- commandArgs(trailingOnly = TRUE)
if (length(args) < 1) {
  cat("Usage: Rscript analysis/summary.R <results.csv|results.json>\n")
  quit(status = 1)
}

path <- args[1]
if (!file.exists(path)) {
  cat(sprintf("File not found: %s\n", path))
  quit(status = 1)
}

df <- read_benchmark(path)

# --- Instance-level aggregation (first run per instance) ---
df1 <- df[df$run == 1 | is.na(df$run), ]

n_total    <- nrow(df1)
n_solved   <- sum(df1$solution_type == "SOLVED",     na.rm = TRUE)
n_unwin    <- sum(df1$solution_type == "UNWINNABLE", na.rm = TRUE)
n_timeout  <- sum(df1$solution_type == "TIMEOUT",    na.rm = TRUE)

solved_times <- df1$time_us[df1$solution_type == "SOLVED"]
timeout_ms <- if ("timeout_ms" %in% names(df1)) max(df1$timeout_ms, na.rm = TRUE) else 60000

cat(sprintf("\n=== Benchmark Summary: %s ===\n", basename(path)))
cat(sprintf("Instances:     %d\n", n_total))
cat(sprintf("Solved:        %d (%.1f%%)\n", n_solved,   100 * n_solved   / n_total))
cat(sprintf("Unwinnable:    %d (%.1f%%)\n", n_unwin,    100 * n_unwin    / n_total))
cat(sprintf("Timeout:       %d (%.1f%%)\n", n_timeout,  100 * n_timeout  / n_total))

cat("\nTiming (us):\n")
cat(sprintf("  Geometric mean:   %.1f\n", geometric_mean(df1$time_us)))
cat(sprintf("  Median:           %.1f\n", median(df1$time_us, na.rm = TRUE)))
cat(sprintf("  PAR2 score:       %.1f\n", par2_score(df1$time_us, df1$solution_type, timeout_ms)))

cat("\nNodes:\n")
cat(sprintf("  Geometric mean:   %.0f\n", geometric_mean(df1$nodes)))
cat(sprintf("  Median:           %.0f\n", median(df1$nodes, na.rm = TRUE)))
cat(sprintf("  Aggregate NPS:    %.0f\n", aggregate_nps(df1$nodes, df1$time_us)))

if ("resident_memory_bytes" %in% names(df1) && any(!is.na(df1$resident_memory_bytes))) {
  mb <- df1$resident_memory_bytes / 1024 / 1024
  cat("\nMemory (resident, MB):\n")
  cat(sprintf("  Median:           %.1f\n", median(mb, na.rm = TRUE)))
  cat(sprintf("  Max:              %.1f\n", max(mb, na.rm = TRUE)))
}

cat("\n")
```

### `analysis/benchmark.R`

```r
#!/usr/bin/env Rscript
# Full comparison between baseline and current benchmark results.
# Usage: Rscript analysis/benchmark.R \
#            --baseline results/baseline.csv \
#            --current  results/current.csv \
#            --output   results/comparison.html \
#            [--metric time_us] [--timeout-ms 60000]

suppressPackageStartupMessages({
  library(optparse)
  library(ggplot2)
})

# Source functions relative to this script's location
initial_options <- commandArgs(trailingOnly = FALSE)
script_dir <- dirname(sub("--file=", "", initial_options[grep("--file=", initial_options)]))
if (length(script_dir) == 0) script_dir <- "."
source(file.path(script_dir, "functions.R"))

option_list <- list(
  make_option("--baseline",   type = "character", help = "Baseline CSV or JSON"),
  make_option("--current",    type = "character", help = "Current CSV or JSON"),
  make_option("--output",     type = "character", default = "comparison.html",
              help = "Output HTML report path"),
  make_option("--metric",     type = "character", default = "time_us",
              help = "Primary metric column [default: time_us]"),
  make_option("--timeout-ms", type = "integer",   default = 60000,
              help = "Timeout in ms for PAR2 [default: 60000]")
)

opt <- parse_args(OptionParser(option_list = option_list))

if (is.null(opt$baseline) || is.null(opt$current)) {
  stop("--baseline and --current are required")
}

base_df <- read_benchmark(opt$baseline)
curr_df <- read_benchmark(opt$current)

# Use first run per instance
b1 <- base_df[base_df$run == 1 | is.na(base_df$run), ]
c1 <- curr_df[curr_df$run == 1 | is.na(curr_df$run), ]

metric <- opt$metric
timeout_ms <- opt$`timeout-ms`

# --- Core statistics ---
b_gm  <- geometric_mean(b1[[metric]])
c_gm  <- geometric_mean(c1[[metric]])
b_med <- median(b1[[metric]], na.rm = TRUE)
c_med <- median(c1[[metric]], na.rm = TRUE)
b_par <- par2_score(b1$time_us, b1$solution_type, timeout_ms)
c_par <- par2_score(c1$time_us, c1$solution_type, timeout_ms)

speedup <- tryCatch({
  merged <- merge(b1[, c("instance", metric)],
                  c1[, c("instance", metric)],
                  by = "instance", suffixes = c("_b", "_c"))
  speedup_ci(merged[[paste0(metric, "_b")]], merged[[paste0(metric, "_c")]])
}, error = function(e) list(estimate = NA, lower = NA, upper = NA))

wtest <- tryCatch(wilcoxon_paired(b1, c1),
                  error = function(e) list(p_value = NA, estimate = NA))

# Solution type comparison
sol_comp <- table(Baseline = b1$solution_type[match(c1$instance, b1$instance)],
                  Current  = c1$solution_type)

# --- Scatter plot ---
plot_df <- merge(b1[, c("instance", metric)],
                 c1[, c("instance", metric)],
                 by = "instance", suffixes = c("_baseline", "_current"))
names(plot_df)[2:3] <- c("baseline", "current")

p <- ggplot(plot_df, aes(x = baseline, y = current)) +
  geom_point(alpha = 0.5) +
  geom_abline(slope = 1, intercept = 0, linetype = "dashed", colour = "red") +
  scale_x_log10() + scale_y_log10() +
  labs(title = sprintf("Baseline vs Current: %s (log scale)", metric),
       x = "Baseline", y = "Current") +
  theme_minimal()

plot_path <- sub("\\.html$", "_scatter.png", opt$output)
ggsave(plot_path, p, width = 6, height = 6, dpi = 150)

# --- HTML report (simple, no rmarkdown dependency) ---
html <- sprintf('<!DOCTYPE html>
<html><head><meta charset="utf-8">
<title>Benchmark Comparison</title>
<style>
  body { font-family: sans-serif; max-width: 900px; margin: 40px auto; }
  table { border-collapse: collapse; width: 100%%; }
  th, td { border: 1px solid #ccc; padding: 6px 12px; text-align: right; }
  th { background: #f0f0f0; text-align: left; }
  td:first-child { text-align: left; }
  h2 { margin-top: 2em; }
</style></head><body>
<h1>Benchmark Comparison Report</h1>
<p>Baseline: <code>%s</code><br>Current: <code>%s</code></p>

<h2>Summary</h2>
<table>
<tr><th>Metric</th><th>Baseline</th><th>Current</th><th>Speedup</th></tr>
<tr><td>Geometric mean %s</td><td>%.1f</td><td>%.1f</td>
    <td>%.3fx [%.3f, %.3f]</td></tr>
<tr><td>Median %s</td><td>%.1f</td><td>%.1f</td><td>—</td></tr>
<tr><td>PAR2 score (us)</td><td>%.1f</td><td>%.1f</td><td>—</td></tr>
</table>

<h2>Statistical Test</h2>
<p>Wilcoxon signed-rank test (paired): p = %.4f</p>
<p>Geometric mean speedup: %.3fx (95%% CI: %.3f – %.3f)</p>

<h2>Solution Type Agreement</h2>
<pre>%s</pre>

<h2>Scatter Plot (%s, log scale)</h2>
<img src="%s" style="max-width:100%%">

</body></html>',
  opt$baseline, opt$current,
  metric, b_gm, c_gm,
  ifelse(is.na(speedup$estimate), NA, speedup$estimate),
  ifelse(is.na(speedup$lower),    NA, speedup$lower),
  ifelse(is.na(speedup$upper),    NA, speedup$upper),
  metric, b_med, c_med,
  b_par, c_par,
  ifelse(is.na(wtest$p_value), NA, wtest$p_value),
  ifelse(is.na(speedup$estimate), NA, speedup$estimate),
  ifelse(is.na(speedup$lower),    NA, speedup$lower),
  ifelse(is.na(speedup$upper),    NA, speedup$upper),
  paste(capture.output(print(sol_comp)), collapse = "\n"),
  metric,
  basename(plot_path)
)

writeLines(html, opt$output)
cat(sprintf("Report written to: %s\n", opt$output))
cat(sprintf("Geometric mean speedup: %.3fx [%.3f, %.3f]\n",
    speedup$estimate, speedup$lower, speedup$upper))
cat(sprintf("Wilcoxon p-value: %.4f\n", wtest$p_value))
```

---

## Step 6: Housekeeping

### `.gitignore`

Add to the existing `.gitignore`:
```
results/
```

If a `results/` directory does not already exist in the repo root, create
a `results/.gitkeep` placeholder file so the directory is tracked.

### `compare_benchmarks.py` deprecation notice

Add these lines at the very top of `scripts/compare_benchmarks.py` (after
the shebang line if present):

```python
# DEPRECATED: This script is superseded by run_benchmark.py + analysis/benchmark.R
# See docs/benchmarking/active/design.md §7 for the replacement workflow.
# This file will be removed in a future cleanup.
```

### `CLAUDE.md` — add benchmark section

Append the following section to `CLAUDE.md` in the repo root (before any
final section, or at the end):

```markdown
## Benchmarking

Run benchmarks via the Python orchestration script (branch: `benchmark-python`):

```bash
# Run on 150 seeds, write CSV
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-150 --timeout 60000 \
    --output results/current.csv

# Quick R summary (also runs automatically at end of run_benchmark.py)
Rscript analysis/summary.R results/current.csv

# Full comparison report
Rscript analysis/benchmark.R \
    --baseline results/baseline.csv \
    --current  results/current.csv \
    --output   results/comparison.html
```

See `docs/benchmarking/active/quickstart.md` for more.
```

---

## Step 7: Build and smoke test

```bash
# Build
./build.sh --release

# Smoke test --json output
./cmake-build-release/bin/solvitaire --type klondike --random 1 --json
# Must be valid JSON; must contain: solution_type, states_searched,
# unique_states, backtracks, max_depth, dominance_moves,
# states_removed_from_cache, cache_size, cache_buckets,
# final_depth, solver_resident_bytes

# Smoke test Python script (5 seeds)
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-5 --timeout 5000 \
    --output /tmp/smoke_test.csv --no-summary
# Must produce /tmp/smoke_test.csv with header row + 5 data rows

# Check CSV has correct columns
head -2 /tmp/smoke_test.csv
```

---

## What NOT to do

- Do not modify `CMakeLists.txt`, `Dockerfile`, CI files, or regression tests.
- Do not modify `solver.h`, `solver.cpp`, `game_state.*`, or any cache files.
- Do not add new CLI flags to `command_line_helper`.
- Do not implement `--benchmark-seeds` or any multi-run orchestration in C++.
- Do not install R packages or require internet access during the build.
- The R scripts must not call `install.packages()` at runtime — they should
  fail gracefully with a helpful message if packages are missing.
