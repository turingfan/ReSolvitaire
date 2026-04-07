#!/usr/bin/env Rscript
# compare_caches.R — Compare cache configurations from remote_benchmark.py output.
# Uses base R only — no package dependencies.
#
# Usage:
#   Rscript analysis/compare_caches.R results/20260407/combined.csv

args <- commandArgs(trailingOnly = TRUE)
if (length(args) == 0) {
    cat("Usage: Rscript compare_caches.R <combined.csv>\n")
    quit(status = 1)
}

df <- read.csv(args[1], stringsAsFactors = FALSE)
cat(sprintf("Loaded %d rows from %s\n\n", nrow(df), args[1]))

df$states_searched <- suppressWarnings(as.numeric(df$states_searched))
df$wall_ms         <- suppressWarnings(as.numeric(df$wall_ms))
df$solution_ms     <- suppressWarnings(as.numeric(df$solution_ms))

# Normalise outcome: solver outputs "winnable"/"unsolvable"; accept both forms
df$outcome[df$outcome == "winnable"]   <- "solved"
df$outcome[df$outcome == "unsolvable"] <- "unsolved"

games   <- sort(unique(df$game))
configs <- sort(unique(df$cache))

# ── Helper ────────────────────────────────────────────────────────────────────
med <- function(x) round(median(x, na.rm = TRUE))

# ── 1. Outcome summary ────────────────────────────────────────────────────────
cat("=== Outcome Summary (% solved) ===\n")
rows <- list()
for (g in games) for (c in configs) {
    sub <- df[df$game == g & df$cache == c, ]
    if (nrow(sub) == 0) next
    solved <- sum(sub$outcome == "solved")
    rows[[length(rows)+1]] <- data.frame(
        game=g, cache=c, n=nrow(sub), solved=solved,
        pct_solved=round(100*solved/nrow(sub), 1),
        timed_out=sum(sub$outcome %in% c("timeout","unsolved")),
        stringsAsFactors=FALSE)
}
print(do.call(rbind, rows), row.names=FALSE)
cat("\n")

# ── 2. Speed on solved instances ──────────────────────────────────────────────
cat("=== Speed: Median Wall Time on Solved Instances (ms) ===\n")
rows <- list()
for (g in games) for (c in configs) {
    sub <- df[df$game == g & df$cache == c & df$outcome == "solved", ]
    if (nrow(sub) == 0) next
    rows[[length(rows)+1]] <- data.frame(
        game=g, cache=c, n_solved=nrow(sub),
        median_ms=med(sub$wall_ms),
        p75_ms=round(quantile(sub$wall_ms, 0.75, na.rm=TRUE)),
        p95_ms=round(quantile(sub$wall_ms, 0.95, na.rm=TRUE)),
        stringsAsFactors=FALSE)
}
print(do.call(rbind, rows), row.names=FALSE)
cat("\n")

# ── 3. Speedup: auto vs force-lru ─────────────────────────────────────────────
cat("=== Speedup: auto vs force-lru (median wall ms on solved) ===\n")
rows <- list()
for (g in games) {
    auto <- df[df$game==g & df$cache=="auto"      & df$outcome=="solved", "wall_ms"]
    lru  <- df[df$game==g & df$cache=="force-lru" & df$outcome=="solved", "wall_ms"]
    if (length(auto) == 0 || length(lru) == 0) next
    auto_ms <- med(auto); lru_ms <- med(lru)
    rows[[length(rows)+1]] <- data.frame(
        game=g, auto_ms=auto_ms, lru_ms=lru_ms,
        speedup=round(lru_ms/auto_ms, 2),
        stringsAsFactors=FALSE)
}
if (length(rows) == 0) { cat("(no data)\n\n") } else {
    out <- do.call(rbind, rows)
    print(out[order(-out$speedup), ], row.names=FALSE)
    cat("\n")
}

# ── 4. States searched: auto vs force-lru ─────────────────────────────────────
cat("=== States Searched: median (auto vs force-lru) ===\n")
rows <- list()
for (g in games) {
    auto <- df[df$game==g & df$cache=="auto"      & df$outcome=="solved" & !is.na(df$states_searched), "states_searched"]
    lru  <- df[df$game==g & df$cache=="force-lru" & df$outcome=="solved" & !is.na(df$states_searched), "states_searched"]
    if (length(auto) == 0 || length(lru) == 0) next
    rows[[length(rows)+1]] <- data.frame(
        game=g, auto_states=med(auto), lru_states=med(lru),
        ratio=round(med(lru)/med(auto), 2),
        stringsAsFactors=FALSE)
}
if (length(rows) == 0) { cat("(no data)\n\n") } else {
    out <- do.call(rbind, rows)
    print(out[order(-out$ratio), ], row.names=FALSE)
    cat("\n")
}

# ── 5. hash-only vs auto ──────────────────────────────────────────────────────
if ("hash-only" %in% configs) {
    cat("=== hash-only vs auto: speedup on solved instances ===\n")
    rows <- list()
    for (g in games) {
        auto <- df[df$game==g & df$cache=="auto"      & df$outcome=="solved", "wall_ms"]
        ho   <- df[df$game==g & df$cache=="hash-only" & df$outcome=="solved", "wall_ms"]
        if (length(auto) == 0 || length(ho) == 0) next
        rows[[length(rows)+1]] <- data.frame(
            game=g, auto_ms=med(auto), ho_ms=med(ho),
            speedup_ho_vs_auto=round(med(auto)/med(ho), 2),
            stringsAsFactors=FALSE)
    }
    if (length(rows) == 0) { cat("(no data)\n\n") } else {
        out <- do.call(rbind, rows)
        print(out[order(-out$speedup_ho_vs_auto), ], row.names=FALSE)
        cat("\n")
    }
}

# ── 6. Overall totals ─────────────────────────────────────────────────────────
cat("=== Overall Totals by Cache Config ===\n")
rows <- list()
for (c in configs) {
    sub <- df[df$cache == c, ]
    solved <- sub[sub$outcome == "solved", ]
    rows[[length(rows)+1]] <- data.frame(
        cache=c, total=nrow(sub), solved=nrow(solved),
        pct_solved=round(100*nrow(solved)/nrow(sub), 1),
        median_ms=med(solved$wall_ms),
        stringsAsFactors=FALSE)
}
print(do.call(rbind, rows), row.names=FALSE)
