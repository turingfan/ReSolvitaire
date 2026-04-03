# CSV Output Schema

One row per run. Column headers always present by default (`--no-header` to suppress).

## Instance identification

| Column | Type | Example | Notes |
|---|---|---|---|
| `instance` | string | `klondike_42` | Seed-based: `{type}_{seed}`. File-based: basename without extension. |
| `seed` | int or blank | `42` | Blank for file-based instances. |
| `run` | int | `1` | 1-based run index within this instance. Warmup runs are excluded. |

## Outcome

| Column | Type | Values | Notes |
|---|---|---|---|
| `solution_type` | string | `SOLVED`, `UNWINNABLE`, `TIMEOUT` | From solver JSON output. |

## Timing

| Column | Type | Example | Notes |
|---|---|---|---|
| `time_us` | float | `1423.7` | Wall-clock time in microseconds. **Python-measured** (wraps subprocess call). Includes subprocess start-up overhead (~1–5 ms); consistent across runs for relative comparisons. |

## Search statistics (from solver `--json` output)

| Column | Type | Notes |
|---|---|---|
| `nodes` | int | Total states visited. |
| `unique_nodes` | int | Unique states (= final cache occupied count). |
| `backtracks` | int | Backtrack count. |
| `dominance_moves` | int | Dominance moves applied. |
| `states_removed_from_cache` | int | Cache evictions (replacement policy). |
| `cache_size` | int | Final cache occupied entries. |
| `cache_buckets` | int | Total cache bucket count (= 2 × cluster count for flat_cache). |
| `max_depth` | int | Maximum search depth reached. |
| `final_depth` | int | Search depth at termination. |

## Memory

Memory tracking is multi-source. Internal C++ tracking has historically been
unreliable on macOS; Python-measured RSS is the primary metric.

| Column | Type | Source | Notes |
|---|---|---|---|
| `resident_memory_bytes` | int | Python `resource.getrusage(RUSAGE_CHILDREN).ru_maxrss` | Primary. Peak RSS of the solver subprocess. Unit is bytes on Linux, kilobytes on macOS — normalised to bytes by the script. |
| `virtual_memory_bytes` | int | Python `resource` module | Virtual memory size. Less meaningful for cache-heavy workloads; included for completeness. |
| `solver_resident_bytes` | int | C++ `getrusage(RUSAGE_SELF)` in solver `--json` output | Diagnostic. May undercount on macOS (reports only physical pages, not reserved). Compare against `resident_memory_bytes` to detect discrepancies. |

## Configuration

| Column | Type | Example | Notes |
|---|---|---|---|
| `streamliner` | string | `none` | Streamliner setting passed to solver. |
| `cache_capacity` | int | `100000000` | `--cache-capacity` value. |
| `timeout_ms` | int | `60000` | Timeout used for this run. |
| `solver_commit` | string | `bf4f811` | 7-char git hash of HEAD when `run_benchmark.py` was invoked. Identifies the solver binary used. |

## Notes on derived metrics

The following are **not** in the CSV — they are computed in R:

- **NPS (nodes per second):** `nodes / (time_us / 1e6)`
- **Geometric mean:** `exp(mean(log(time_us)))` across instances
- **PAR2 score:** timeout instances penalised at `2 × timeout_ms × 1000` (in us)
- **Speedup:** `geometric_mean(baseline_time) / geometric_mean(current_time)`

See [r_analysis.md](r_analysis.md) for how these are computed.
