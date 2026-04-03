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
