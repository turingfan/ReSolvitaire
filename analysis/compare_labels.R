#!/usr/bin/env Rscript
# compare_labels.R — Compare benchmark configurations based on the 'label' column.
# Uses shared functions from analysis/functions.R for consistency (geo-mean, par2, nps).
#
# Usage:
#   Rscript analysis/compare_labels.R results/20260407/combined.csv

options("width"=10000)

initial_options <- commandArgs(trailingOnly = FALSE)
script_dir <- dirname(normalizePath(sub("--file=", "", initial_options[grep("--file=", initial_options)])))
if (length(script_dir) == 0 || script_dir == "") script_dir <- "."
source(file.path(script_dir, "functions.R"))

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

df <- read_benchmark(file_path)
cat(sprintf("Loaded %d rows from %s\n\n", nrow(df), file_path))

# Ensure numeric columns
df$nodes   <- suppressWarnings(as.numeric(df$nodes))
df$time_us <- suppressWarnings(as.numeric(df$time_us))
df$wall_ms <- df$time_us / 1000

# Normalise outcome
df$outcome <- tolower(df$solution_type)
df$outcome[df$outcome == "winnable"]   <- "SOLVED"
df$outcome[df$outcome == "unsolvable"] <- "UNWINNABLE"
df$outcome[df$outcome == "timeout"]    <- "TIMEOUT"
df$outcome <- toupper(df$outcome)

# Extract base game name from instance (e.g., 'free-cell_10' -> 'free-cell')
# Logic: if seed is present, use game field if it exists, otherwise strip suffix
if (!"game" %in% names(df)) {
    df$game <- sub("_[0-9]+$", "", df$instance)
}

games  <- sort(unique(df$game))
labels <- sort(unique(df$label))

# Only look at first run per instance
df1 <- df[df$run == 1 | is.na(df$run), ]

# ── 1. Outcome summary ────────────────────────────────────────────────────────
cat("=== Outcome Summary (% solved) ===\n")
rows <- list()
for (g in games) {
    for (l in labels) {
        sub <- df1[df1$game == g & df1$label == l, ]
        if (nrow(sub) == 0) next
        n_total   <- nrow(sub)
        n_solved  <- sum(sub$outcome == "SOLVED", na.rm = TRUE)
        n_unwin   <- sum(sub$outcome == "UNWINNABLE", na.rm = TRUE)
        n_timeout <- sum(sub$outcome == "TIMEOUT", na.rm = TRUE)
        
        rows[[length(rows)+1]] <- data.frame(
            game=g, label=l, n=n_total, 
            solved=sprintf("%d (%.1f%%)", n_solved, 100*n_solved/n_total),
            unwin=sprintf("%d (%.1f%%)", n_unwin, 100*n_unwin/n_total),
            timeout=sprintf("%d (%.1f%%)", n_timeout, 100*n_timeout/n_total),
            stringsAsFactors=FALSE)
    }
}
if (length(rows) > 0) print(do.call(rbind, rows), row.names=FALSE)
cat("\n")

# ── 2. Detailed Stats per Game & Label ────────────────────────────────────────
cat("=== Detailed Statistics (Geometric Mean, PAR2, NPS) ===\n")
rows <- list()
for (g in games) {
    for (l in labels) {
        sub <- df1[df1$game == g & df1$label == l, ]
        if (nrow(sub) == 0) next
        
        timeout_ms <- if ("timeout_ms" %in% names(sub)) max(sub$timeout_ms, na.rm = TRUE) else 60000
        
        rows[[length(rows)+1]] <- data.frame(
            game=g, label=l,
            time_geo_ms=round(geometric_mean(sub$time_us)/1000, 1),
            par2_ms=round(par2_score(sub$time_us, sub$outcome, timeout_ms)/1000, 1),
            nodes_geo=round(geometric_mean(sub$nodes)),
            agg_nps=round(aggregate_nps(sub$nodes, sub$time_us)),
            stringsAsFactors=FALSE)
    }
}
if (length(rows) > 0) print(do.call(rbind, rows), row.names=FALSE)
cat("\n")

# ── 3. Pairwise Comparison relative to Baseline ──────────────────────────────
baseline_label <- if ("auto" %in% labels) "auto" else labels[1]
other_labels   <- labels[labels != baseline_label]

if (length(other_labels) > 0) {
    cat(sprintf("=== Comparison against baseline: '%s' ===\n", baseline_label))
    cat("Metric ratios: > 1.0 means improvement (labels are faster/higher NPS, or fewer nodes)\n\n")
    
    rows <- list()
    for (g in games) {
        base_sub <- df1[df1$game == g & df1$label == baseline_label, ]
        if (nrow(base_sub) == 0) next
        
        base_time <- geometric_mean(base_sub$time_us)
        base_nodes <- geometric_mean(base_sub$nodes)
        base_nps <- aggregate_nps(base_sub$nodes, base_sub$time_us)
        
        for (l in other_labels) {
            tgt_sub <- df1[df1$game == g & df1$label == l, ]
            if (nrow(tgt_sub) == 0) next
            
            tgt_time <- geometric_mean(tgt_sub$time_us)
            tgt_nodes <- geometric_mean(tgt_sub$nodes)
            tgt_nps <- aggregate_nps(tgt_sub$nodes, tgt_sub$time_us)
            
            # Outcome difference check (on matched seeds)
            merged <- merge(base_sub[, c("instance", "outcome")], 
                          tgt_sub[, c("instance", "outcome")], 
                          by="instance", suffixes=c("_b", "_t"))
            diff_count <- sum(merged$outcome_b != merged$outcome_t, na.rm=TRUE)

            rows[[length(rows)+1]] <- data.frame(
                game=g, label=l,
                time_speedup=round(base_time / tgt_time, 2),
                node_reduction=round(base_nodes / tgt_nodes, 2),
                nps_gain=round(tgt_nps / base_nps, 2),
                result_diffs=diff_count,
                stringsAsFactors=FALSE)
        }
    }
    if (length(rows) > 0) {
        print(do.call(rbind, rows), row.names=FALSE)
    } else {
        cat("(no matched instances for comparison)\n")
    }
    cat("\n")
}

# ── 4. Overall totals ─────────────────────────────────────────────────────────
cat("=== Overall Totals by Label ===\n")
rows <- list()
for (l in labels) {
    sub <- df1[df1$label == l, ]
    if (nrow(sub) == 0) next
    timeout_ms <- if ("timeout_ms" %in% names(sub)) max(sub$timeout_ms, na.rm = TRUE) else 60000
    
    rows[[length(rows)+1]] <- data.frame(
        label=l, n=nrow(sub), 
        pct_solved=round(100*sum(sub$outcome == "SOLVED", na.rm=TRUE)/nrow(sub), 1),
        geo_time_ms=round(geometric_mean(sub$time_us)/1000, 1),
        par2_ms=round(par2_score(sub$time_us, sub$outcome, timeout_ms)/1000, 1),
        agg_nps=round(aggregate_nps(sub$nodes, sub$time_us)),
        stringsAsFactors=FALSE)
}
if (length(rows) > 0) print(do.call(rbind, rows), row.names=FALSE)
cat("\n")
