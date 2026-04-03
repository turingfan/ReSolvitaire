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
