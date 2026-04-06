# R Analysis Scripts

## Scripts

| Script | Invoked by | Purpose |
|---|---|---|
| `analysis/functions.R` | sourced by other scripts | Shared statistical utilities |
| `analysis/summary.R` | `run_benchmark.py` (automatic) + manual | Quick single-file summary |
| `analysis/benchmark.R` | Manual | Full two-file comparison report |

## Installation

```r
install.packages(c("ggplot2", "rmarkdown", "boot", "optparse"))
```

R itself: `brew install r` (macOS) or `sudo apt-get install r-base` (Linux).

---

## `analysis/summary.R`

Prints a concise summary table to stdout. Called automatically at the end of
every `run_benchmark.py` run (unless `--no-summary` is passed).

```bash
Rscript analysis/summary.R results/klondike_current.csv
```

Accepts CSV or JSON (detected by file extension).

**Output example:**
```
=== Benchmark Summary: klondike_current.csv ===
Instances:     150
Solved:        127 (84.7%)
Unwinnable:     20 (13.3%)
Timeout:         3 (2.0%)

Timing (us):
  Geometric mean:   1 423.7
  Median:           1 210.4
  PAR2 score:       3 891.2

Nodes:
  Geometric mean:    8 432
  Median:            7 109
  Aggregate NPS:   5 924 183

Memory (resident, MB):
  Median:           312.4
  Max:              401.1
```

---

## `analysis/benchmark.R`

Full comparison between baseline and current result sets.

```bash
Rscript analysis/benchmark.R \
    --baseline results/baseline.csv \
    --current  results/current.csv \
    --output   results/comparison.html \
    [--metric time_us]        # primary metric (default: time_us)
    [--timeout-ms 60000]      # for PAR2 computation
```

Accepts CSV or JSON for both inputs.

**HTML report contents:**
1. Summary table — geometric mean, PAR2, median, solve rates for both
2. Geometric mean speedup with bootstrap 95% CI
3. Wilcoxon signed-rank test (p-value, effect size)
4. Per-instance scatter plot (baseline vs current time, log scale)
5. Solution type agreement table (how many instances changed outcome)
6. Per-instance detail table (sortable, filterable in HTML)

---

## `analysis/functions.R`

Utility functions sourced by `summary.R` and `benchmark.R`.

### `read_benchmark(path)`

Reads a benchmark result file. Detects format by extension (`.csv` → `read.csv`,
`.json` → `jsonlite::fromJSON`). Returns a data frame with the standard schema.

### `geometric_mean(x, na.rm = TRUE)`

Handles zeros and NA values. Returns `NA` if all values are non-positive.

### `par2_score(times_us, timeout_ms, solution_types)`

PAR2: solved instances use actual time; timeout/unwinnable instances are
penalised at `2 × timeout_ms × 1000` microseconds. Returns mean PAR2 score
across all instances.

### `wilcoxon_paired(baseline, current)`

Paired Wilcoxon signed-rank test on matched instances. Returns list with
`p_value`, `statistic`, `estimate` (pseudo-median of differences).

### `speedup_ci(baseline_times, current_times, R = 1000)`

Bootstrap 95% CI for geometric mean speedup (`baseline / current`). Returns
`list(estimate, lower, upper)`.

---

## Extending the R scripts

### Adding a new metric to the summary

Edit `analysis/summary.R` in the section marked `# --- Custom metrics ---`.
The data frame `df` has all CSV columns available.

### Adding a new plot to the comparison report

Edit `analysis/benchmark.R`. Plots use ggplot2. Add a new `ggplot(...)` block
and include it in the RMarkdown template section.

### Adding a new statistical test

Add a function to `analysis/functions.R` and call it from `benchmark.R`.
Follow the existing pattern: return a named list, render it in the report with
`knitr::kable`.
