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

# PAR2 score: instances the solver did not decide are penalised at
# 2 * timeout_ms * 1000 us.
#
# UNWINNABLE is deliberately NOT penalised: proving an instance unwinnable is a
# decisive answer, as much a success as finding a solution. (Before 2026-08-10
# it was penalised, which made PAR2 meaningless on games with a large
# unwinnable fraction — e.g. klondike at ~19% unwinnable, where the penalty
# alone dominated the score.) FAILED is the solver's own MEM_LIMIT and KILLED
# is an external kill: neither decided the instance, so both are penalised.
PAR2_UNSOLVED <- c("TIMEOUT", "FAILED", "KILLED", "ERROR", "UNKNOWN")

par2_score <- function(times_us, solution_types, timeout_ms,
                       unsolved_types = PAR2_UNSOLVED) {
  penalty <- 2 * timeout_ms * 1000
  adjusted <- ifelse(solution_types %in% unsolved_types, penalty, times_us)
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
  ci <- tryCatch({
    result <- boot::boot.ci(b, type = "perc")$percent[4:5]
    if (is.null(result) || length(result) < 2) c(NA_real_, NA_real_) else result
  }, error = function(e) c(NA_real_, NA_real_))
  list(estimate = geometric_mean(ratios), lower = ci[1], upper = ci[2])
}
