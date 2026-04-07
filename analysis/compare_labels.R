#!/usr/bin/env Rscript
# compare_labels.R — Compare benchmark configurations based on the 'label' column.
# Uses base R only — no package dependencies.
#
# Usage:
#   Rscript analysis/compare_labels.R results/20260407/combined.csv

args <- commandArgs(trailingOnly = TRUE)
if (length(args) == 0) {
    cat("Usage: Rscript analysis/compare_labels.R <combined.csv>\n")
    quit(status = 1)
}

file_path <- args[1]
if (!file.exists(file_path)) {
    cat(sprintf("Error: file '%s' not found.\n", file_path))
    quit(status = 1)
}

df <- read.csv(file_path, stringsAsFactors = FALSE)
cat(sprintf("Loaded %d rows from %s\n\n", nrow(df), file_path))

# Convert numeric columns
df$nodes   <- suppressWarnings(as.numeric(df$nodes))
df$time_us <- suppressWarnings(as.numeric(df$time_us))

# Convert time from microseconds to milliseconds for readability
df$wall_ms <- df$time_us / 1000

# Normalise outcome
df$outcome <- tolower(df$solution_type)
df$outcome[df$outcome == "winnable"]   <- "solved"
df$outcome[df$outcome == "unsolvable"] <- "unsolved"

# Extract base game name from instance (e.g., 'free-cell_10' -> 'free-cell')
df$game <- sub("_[0-9]+$", "", df$instance)

games  <- sort(unique(df$game))
labels <- sort(unique(df$label))

# ── Helper ────────────────────────────────────────────────────────────────────
med <- function(x) round(median(x, na.rm = TRUE))

# ── 1. Outcome summary ────────────────────────────────────────────────────────
cat("=== Outcome Summary (% solved) ===\n")
rows <- list()
for (g in games) {
    for (l in labels) {
        sub <- df[df$game == g & df$label == l, ]
        if (nrow(sub) == 0) next
        solved <- sum(sub$outcome == "solved")
        rows[[length(rows)+1]] <- data.frame(
            game=g, label=l, n=nrow(sub), solved=solved,
            pct_solved=round(100*solved/nrow(sub), 1),
            timed_out=sum(sub$outcome %in% c("timeout","unsolved")),
            stringsAsFactors=FALSE)
    }
}
if (length(rows) > 0) print(do.call(rbind, rows), row.names=FALSE)
cat("\n")

# ── 2. Speed on solved instances ──────────────────────────────────────────────
cat("=== Speed: Median Wall Time on Solved Instances (ms) ===\n")
rows <- list()
for (g in games) {
    for (l in labels) {
        sub <- df[df$game == g & df$label == l & df$outcome == "solved", ]
        if (nrow(sub) == 0) next
        rows[[length(rows)+1]] <- data.frame(
            game=g, label=l, n_solved=nrow(sub),
            median_ms=med(sub$wall_ms),
            p75_ms=round(quantile(sub$wall_ms, 0.75, na.rm=TRUE)),
            p95_ms=round(quantile(sub$wall_ms, 0.95, na.rm=TRUE)),
            stringsAsFactors=FALSE)
    }
}
if (length(rows) > 0) print(do.call(rbind, rows), row.names=FALSE)
cat("\n")

# ── 3. Pairwise Speedup against Baseline ──────────────────────────────────────
# Automatically select "auto" as baseline if it exists, otherwise the first label.
baseline_label <- if ("auto" %in% labels) "auto" else labels[1]
other_labels   <- labels[labels != baseline_label]

if (length(other_labels) > 0) {
    cat(sprintf("=== Speedups relative to baseline: '%s' ===\n", baseline_label))
    cat("Metric: Speedup = (Median Ms of Baseline) / (Median Ms of Label)\n\n")
    
    rows <- list()
    for (g in games) {
        base_df <- df[df$game == g & df$label == baseline_label & df$outcome == "solved", "wall_ms"]
        if (length(base_df) == 0) next
        base_ms <- med(base_df)
        
        for (l in other_labels) {
            tgt_df <- df[df$game == g & df$label == l & df$outcome == "solved", "wall_ms"]
            if (length(tgt_df) == 0) next
            tgt_ms <- med(tgt_df)
            
            rows[[length(rows)+1]] <- data.frame(
                game=g, comparison=sprintf("%s vs %s", l, baseline_label),
                baseline_ms=base_ms, label_ms=tgt_ms,
                speedup=round(base_ms / tgt_ms, 2),
                stringsAsFactors=FALSE)
        }
    }
    if (length(rows) == 0) { cat("(no data for speedup comparison)\n\n") } else {
        out <- do.call(rbind, rows)
        print(out[order(-out$speedup), ], row.names=FALSE)
        cat("\n")
    }
}

# ── 4. States searched relative to baseline ───────────────────────────────────
if (length(other_labels) > 0) {
    cat(sprintf("=== States Searched relative to baseline: '%s' ===\n", baseline_label))
    cat("Metric: Ratio = (Median States of Label) / (Median States of Baseline)\n\n")
    
    rows <- list()
    for (g in games) {
        base_df <- df[df$game == g & df$label == baseline_label & df$outcome == "solved" & !is.na(df$nodes), "nodes"]
        if (length(base_df) == 0) next
        base_st <- med(base_df)
        
        for (l in other_labels) {
            tgt_df <- df[df$game == g & df$label == l & df$outcome == "solved" & !is.na(df$nodes), "nodes"]
            if (length(tgt_df) == 0) next
            tgt_st <- med(tgt_df)
            
            rows[[length(rows)+1]] <- data.frame(
                game=g, comparison=sprintf("%s vs %s", l, baseline_label),
                baseline_nodes=base_st, label_nodes=tgt_st,
                ratio=round(tgt_st / base_st, 2),
                stringsAsFactors=FALSE)
        }
    }
    if (length(rows) == 0) { cat("(no data for node comparison)\n\n") } else {
        out <- do.call(rbind, rows)
        print(out[order(-out$ratio), ], row.names=FALSE)
        cat("\n")
    }
}

# ── 5. Overall totals ─────────────────────────────────────────────────────────
cat("=== Overall Totals by Label ===\n")
rows <- list()
for (l in labels) {
    sub <- df[df$label == l, ]
    solved <- sub[sub$outcome == "solved", ]
    rows[[length(rows)+1]] <- data.frame(
        label=l, total=nrow(sub), solved=nrow(solved),
        pct_solved=round(100*nrow(solved)/nrow(sub), 1),
        median_ms=med(solved$wall_ms),
        stringsAsFactors=FALSE)
}
if (length(rows) > 0) print(do.call(rbind, rows), row.names=FALSE)
cat("\n")
