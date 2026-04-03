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
