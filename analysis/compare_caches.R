#!/usr/bin/env Rscript
# compare_caches.R — Compare cache configurations from remote_benchmark.py output.
#
# Usage:
#   Rscript analysis/compare_caches.R results/20260407/combined.csv
#   Rscript analysis/compare_caches.R results/20260407/combined.csv --output report.html

args <- commandArgs(trailingOnly = TRUE)
if (length(args) == 0) {
    cat("Usage: Rscript compare_caches.R <combined.csv> [--output <file.html>]\n")
    quit(status = 1)
}

csv_file <- args[1]
output_file <- NULL
if (length(args) >= 3 && args[2] == "--output") output_file <- args[3]

suppressPackageStartupMessages({
    library(dplyr)
    library(tidyr)
})

df <- read.csv(csv_file, stringsAsFactors = FALSE)
cat(sprintf("Loaded %d rows from %s\n\n", nrow(df), csv_file))

# Coerce numerics
df$states_searched <- suppressWarnings(as.numeric(df$states_searched))
df$wall_ms         <- suppressWarnings(as.numeric(df$wall_ms))
df$solution_ms     <- suppressWarnings(as.numeric(df$solution_ms))

# ── 1. Outcome summary by game × cache ────────────────────────────────────────
cat("=== Outcome Summary (% solved) ===\n")
outcome_summary <- df %>%
    group_by(game, cache) %>%
    summarise(
        n         = n(),
        solved    = sum(outcome == "solved", na.rm = TRUE),
        pct_solved = round(100 * mean(outcome == "solved", na.rm = TRUE), 1),
        timed_out = sum(outcome %in% c("timeout", "unsolved"), na.rm = TRUE),
        .groups   = "drop"
    ) %>%
    arrange(game, cache)

print(as.data.frame(outcome_summary), row.names = FALSE)
cat("\n")

# ── 2. Speed comparison (median wall time on solved instances) ─────────────────
cat("=== Speed: Median Wall Time on Solved Instances (ms) ===\n")
speed_summary <- df %>%
    filter(outcome == "solved") %>%
    group_by(game, cache) %>%
    summarise(
        n_solved    = n(),
        median_ms   = round(median(wall_ms, na.rm = TRUE)),
        p25_ms      = round(quantile(wall_ms, 0.25, na.rm = TRUE)),
        p75_ms      = round(quantile(wall_ms, 0.75, na.rm = TRUE)),
        p95_ms      = round(quantile(wall_ms, 0.95, na.rm = TRUE)),
        .groups     = "drop"
    ) %>%
    arrange(game, cache)

print(as.data.frame(speed_summary), row.names = FALSE)
cat("\n")

# ── 3. Speedup of auto vs force-lru (per game) ────────────────────────────────
cat("=== Speedup: auto vs force-lru (median wall time ratio) ===\n")
pivot <- df %>%
    filter(outcome == "solved", cache %in% c("auto", "force-lru")) %>%
    group_by(game, cache) %>%
    summarise(median_ms = median(wall_ms, na.rm = TRUE), .groups = "drop") %>%
    pivot_wider(names_from = cache, values_from = median_ms) %>%
    rename(auto_ms = auto, lru_ms = `force-lru`) %>%
    mutate(
        speedup = round(lru_ms / auto_ms, 2),
        faster  = ifelse(speedup > 1, "auto faster", ifelse(speedup < 1, "lru faster", "same"))
    ) %>%
    arrange(desc(speedup))

print(as.data.frame(pivot), row.names = FALSE)
cat("\n")

# ── 4. States searched: auto vs force-lru ────────────────────────────────────
cat("=== States Searched: median (auto vs force-lru) ===\n")
states_pivot <- df %>%
    filter(outcome == "solved", cache %in% c("auto", "force-lru"),
           !is.na(states_searched)) %>%
    group_by(game, cache) %>%
    summarise(median_states = round(median(states_searched)), .groups = "drop") %>%
    pivot_wider(names_from = cache, values_from = median_states) %>%
    rename(auto_states = auto, lru_states = `force-lru`) %>%
    mutate(ratio = round(lru_states / auto_states, 2)) %>%
    arrange(desc(ratio))

print(as.data.frame(states_pivot), row.names = FALSE)
cat("\n")

# ── 5. Hash-only vs auto ──────────────────────────────────────────────────────
if ("hash-only" %in% df$cache) {
    cat("=== hash-only vs auto: speedup on solved instances ===\n")
    ho_pivot <- df %>%
        filter(outcome == "solved", cache %in% c("auto", "hash-only")) %>%
        group_by(game, cache) %>%
        summarise(median_ms = median(wall_ms, na.rm = TRUE), .groups = "drop") %>%
        pivot_wider(names_from = cache, values_from = median_ms) %>%
        rename(auto_ms = auto, ho_ms = `hash-only`) %>%
        mutate(speedup_ho_vs_auto = round(auto_ms / ho_ms, 2)) %>%
        arrange(desc(speedup_ho_vs_auto))
    print(as.data.frame(ho_pivot), row.names = FALSE)
    cat("\n")
}

# ── 6. Overall totals ─────────────────────────────────────────────────────────
cat("=== Overall Totals by Cache Config ===\n")
overall <- df %>%
    group_by(cache) %>%
    summarise(
        total     = n(),
        solved    = sum(outcome == "solved", na.rm = TRUE),
        pct_solved = round(100 * mean(outcome == "solved", na.rm = TRUE), 1),
        median_ms = round(median(wall_ms[outcome == "solved"], na.rm = TRUE)),
        .groups   = "drop"
    )
print(as.data.frame(overall), row.names = FALSE)
