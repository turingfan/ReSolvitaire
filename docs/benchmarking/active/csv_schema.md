# CSV Output Schema — Authoritative Contract

**This document is the single source of truth** for the benchmark CSV format and the
`solution_type` outcome vocabulary. Every surviving benchmark script and the
`ReSolvitaire-bench` hook must conform to what is stated here. Deviations are bugs.

The canonical producer is `scripts/run_benchmark.py`. The canonical consumer of
`solution_type` is the `benchmark` hook in the `ReSolvitaire-bench` repo.

---

## Column order

The header line written by `run_benchmark.py` (line ~392) is:

```
instance,seed,run,solution_type,time_us,nodes,unique_nodes,backtracks,dominance_moves,states_removed_from_cache,cache_size,cache_buckets,max_depth,final_depth,resident_memory_bytes,solver_resident_bytes,streamliner,cache_capacity,timeout_ms,solver_commit,label
```

That is **21 columns**. One row per run. Column headers always present by default
(`--no-header` suppresses them for append mode).

---

## Column definitions (in order)

### Instance identification

| # | Column | Type | Example | Source | Notes |
|---|---|---|---|---|---|
| 1 | `instance` | string | `klondike_42` | run metadata | Seed-based: `{type}_{seed}`. File-based: basename without extension. |
| 2 | `seed` | int or blank | `42` | run metadata | Blank for file-based instances. |
| 3 | `run` | int | `1` | run metadata | 1-based run index within this instance. Warmup runs are excluded from the CSV entirely. |

### Outcome

| # | Column | Type | Values | Source |
|---|---|---|---|---|
| 4 | `solution_type` | string | `SOLVED`, `UNWINNABLE`, `TIMEOUT`, `TERMINATED`, `KILLED`, `UNKNOWN` | See §Outcome vocabulary below |

### Timing

| # | Column | Type | Example | Source | Notes |
|---|---|---|---|---|---|
| 5 | `time_us` | int (written as `%.0f`) | `1423700` | Python `time.perf_counter()` | Wall-clock time in **microseconds**. Measured by the Python wrapper around the subprocess call. Includes subprocess start-up overhead (~1–5 ms); consistent across runs for relative comparisons. |

### Search statistics (from solver `--json` output)

All of these come from the solver's JSON output. They are 0 when `solution_type` is
`KILLED` (no output) or when parsing fails.

| # | Column | Type | Solver JSON field | Notes |
|---|---|---|---|---|
| 6 | `nodes` | int | `states_searched` | Total states visited in DFS. |
| 7 | `unique_nodes` | int | `unique_states` | Unique states searched (final cache occupied count). |
| 8 | `backtracks` | int | `backtracks` | Backtrack count. |
| 9 | `dominance_moves` | int | `dominance_moves` | Dominance moves applied. |
| 10 | `states_removed_from_cache` | int | `states_removed_from_cache` | Cache evictions (replacement policy). |
| 11 | `cache_size` | int | `cache_size` | Final cache occupied entries. |
| 12 | `cache_buckets` | int | `cache_buckets` | Total cache bucket count. (For `flat_cache` this equals 2 × cluster count.) |
| 13 | `max_depth` | int | `max_depth` | Maximum search depth reached. |
| 14 | `final_depth` | int | `final_depth` | Search depth at termination. |

### Memory

| # | Column | Type | Source | Notes |
|---|---|---|---|---|
| 15 | `resident_memory_bytes` | int | `/usr/bin/time -l` (macOS) or `/usr/bin/time -v` (Linux) | **Primary.** Per-run peak RSS of the solver subprocess in bytes. `parse_rss_from_time_output()` extracts this from `/usr/bin/time` stderr. Falls back to 0 if `/usr/bin/time` is unavailable. |
| 16 | `solver_resident_bytes` | int | C++ `getrusage(RUSAGE_SELF)` in solver `--json` output | **Diagnostic.** Solver's own self-reported peak RSS. 0 for legacy runs (no `--json`), 0 on kill. macOS: bytes; Linux: KB×1024 (conversion done in `main.cpp`). |

### Configuration

| # | Column | Type | Example | Source | Notes |
|---|---|---|---|---|---|
| 17 | `streamliner` | string | `none` | `--streamliner` arg | Value passed to `--streamliners` on the solver command line. |
| 18 | `cache_capacity` | int or blank | `100000000` | `--cache-capacity` arg | Blank if `--cache-capacity` was not specified (solver uses its own default). |
| 19 | `timeout_ms` | int | `60000` | `--timeout` arg | Timeout passed to the solver via `--timeout`, in milliseconds. |
| 20 | `solver_commit` | string | `bf4f811` | `git rev-parse --short HEAD` | 7-char git hash of HEAD at the time `run_benchmark.py` was invoked. Identifies which solver binary was used. `"unknown"` if git unavailable. |
| 21 | `label` | string | `flat-cache` | `--label` arg | Freeform tag. Empty string `""` if not provided. Useful for grouping runs from different configurations in a single CSV when using `--append`. |

---

## Outcome vocabulary — end-to-end flow

### Solver JSON emission (`src/main/main.cpp` ~line 364)

The solver writes a JSON object to stdout with `"solution_type"` set to one of:

| Solver string | Meaning |
|---|---|
| `"winnable"` | DFS proved the game is solvable. |
| `"unsolvable"` | DFS proved the game is unsolvable. |
| `"timeout"` | DFS hit `--timeout` cleanly; stats up to that point are valid. |
| `"failed"` | Internal solver error (should not arise in normal use). |

Note: the solver emits **lowercase** strings. The CSV vocabulary uses **uppercase**.

### Mapping in `run_benchmark.py` (`parse_solver_json`, ~line 297)

`parse_solver_json` maps the solver's JSON `"solution_type"` field:

| Solver emits | CSV value |
|---|---|
| `"winnable"` | `SOLVED` |
| `"unsolvable"` | `UNWINNABLE` |
| `"timeout"` | `TIMEOUT` |
| `"failed"` | `UNKNOWN` (falls through to the else branch) |
| JSON unparseable / field missing | `UNKNOWN` |

### Kill-path classification (main loop ~lines 445–473)

The Python wrapper detects three additional situations:

| Situation | CSV value | Stats |
|---|---|---|
| Process returned non-zero exit and produced output that `parse_solver_json` returns `"UNKNOWN"` for | `TERMINATED` | Whatever stats were parsed (may be partial or zero). |
| Process returned non-zero exit and produced **no output** | `KILLED` | All stats set to 0. |

The `TERMINATED` override (line ~452): if the process was signalled but emitted
partial or complete JSON that parses successfully to a known outcome (`SOLVED`,
`UNWINNABLE`, `TIMEOUT`), that outcome is kept as-is. Only if parsing yields
`UNKNOWN` is it overridden to `TERMINATED`.

### Summary table: solver string → CSV value → bench-hook bucket

| Solver emits | CSV `solution_type` | Bench hook counts as | Stats present? |
|---|---|---|---|
| `"winnable"` | `SOLVED` | `solved` | Yes — full stats |
| `"unsolvable"` | `UNWINNABLE` | `unsolvable` | Yes — full stats |
| `"timeout"` | `TIMEOUT` | `timeout` | Yes — partial stats valid |
| `"failed"` | `UNKNOWN` | (not counted separately; `UNKNOWN` is ignored by the hook) | Partial or zero |
| Killed, partial output → parse succeeds | `SOLVED`/`UNWINNABLE`/`TIMEOUT` | as above | Yes |
| Killed, partial output → parse yields UNKNOWN | `TERMINATED` | `terminated` | Partial or zero |
| Killed, no output | `KILLED` | `killed` | All zero |
| JSON present but malformed/field missing | `UNKNOWN` | (not counted) | Zero |

### Semantic distinction: clean vs wrapper-forced termination

- **`TIMEOUT`** — the solver itself hit its `--timeout` deadline, reported it cleanly
  via JSON, and exited normally. Stats fields are populated and represent the search up
  to the timeout. This is the expected outcome for hard instances.

- **`TERMINATED`** — the Python safety valve fired: the solver did not exit within
  2× the timeout, received SIGTERM, and emitted some output before dying. Stats may
  be partial. Work may have been lost. This outcome indicates a solver hang or a wrapper
  timing misconfiguration.

- **`KILLED`** — the solver emitted nothing. Either SIGKILL was sent (after SIGTERM
  timed out) and killed it before any output was flushed, or the process crashed
  silently. All stats fields are 0; the run is essentially data-free.

The goal of the benchmark pipeline is that all hard instances end as `TIMEOUT` (clean,
solver-reported), never as `TERMINATED` or `KILLED`. See the plan document for
Stage 2 kill-discipline improvements.

---

## Notes on derived metrics

The following are **not** in the CSV — they are computed in R by `analysis/summary.R`
and `analysis/benchmark.R`:

- **NPS (nodes per second):** `nodes / (time_us / 1e6)`
- **Geometric mean:** `exp(mean(log(time_us)))` across instances
- **PAR2 score:** timeout instances penalised at `2 × timeout_ms × 1000` (in µs)
- **Speedup:** `geometric_mean(baseline_time) / geometric_mean(current_time)`

See [r_analysis.md](r_analysis.md) for how these are computed.

---

## Conformance gaps and discrepancies (as of 2026-05-29)

The following discrepancies were found during the investigation that produced this
document. They are reported here for the orchestrator; they are **not** fixed by the
documentation task.

1. **`run_benchmark.py` docstring vs code — SIGTERM target.** The `run_solver()`
   docstring (lines ~123–128) says SIGTERM is sent to "the entire process group"
   and SIGKILL is also to "the process group". The actual code (`proc.send_signal(signal.SIGTERM)` and
   `proc.kill()`) signals only the **immediate child** (the `/usr/bin/time` wrapper),
   not a process group. When `/usr/bin/time` is active, the solver grandchild can be
   orphaned and continue running after the wrapper exits. The docstring also contains
   a self-correction note (lines ~131–133) acknowledging this. The docstring claim of
   process-group signalling is inaccurate.

2. **`solution_type` = `UNKNOWN` is silently ignored by the bench hook.** The hook
   (`ReSolvitaire-bench/hooks/benchmark`) counts `SOLVED`, `UNWINNABLE`, `TIMEOUT`,
   `KILLED`, `TERMINATED` but does not count `UNKNOWN`. Rows with `solution_type =
   UNKNOWN` are therefore invisible in the hook's summary. This is a potential
   silent data loss if the solver emits `"failed"` or if JSON is unparseable. The
   hook should arguably bucket `UNKNOWN` explicitly.

3. **`streamliner` vocabulary: `CLAUDE.md` says `smart` but the solver accepts
   `smart-solvability`.** `src/main/input-output/input/command_line_helper.cpp`
   maps CLI string `"smart-solvability"` to the internal `SMART` enum; the JSON
   output then emits `"smart"`. So `run_benchmark.py`'s CLI choice `smart-solvability`
   is correct and consistent with the solver. However, `CLAUDE.md`'s documentation
   of the `--streamliners` option lists `smart` as the CLI token, which is wrong —
   the CLI token is `smart-solvability`. The CSV `streamliner` column will contain
   `smart-solvability` (whatever `run_benchmark.py` received), while the solver's
   own JSON output uses `smart`. These are not compared against each other in the
   pipeline, but it is a vocabulary inconsistency worth noting in `CLAUDE.md`.

4. **`time_us` is integer-rounded in the CSV.** The column is documented as type
   `float` in the existing schema and named with `_us` suffix suggesting precision,
   but the row is written as `{time_us:.0f}` (no decimal places). Consumers should
   treat it as a rounded integer. The R analysis scripts should be confirmed to read
   it accordingly.

5. **`cache_capacity` is blank when unspecified.** The CSV emits an empty string for
   `cache_capacity` when `--cache-capacity` was not passed. Consumers (R scripts,
   `compare_benchmarks.py`) that parse this column must handle blank/NA values.
   This is implicit behaviour, not explicitly documented in the original schema.
