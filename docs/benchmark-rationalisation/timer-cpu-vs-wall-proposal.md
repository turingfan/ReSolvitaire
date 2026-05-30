# Proposal: solver `--timeout` on CPU time instead of wall clock

**Status:** PROPOSAL for Ian's decision — not implemented. Raised 2026-05-30 while
debugging KILLED runs (see `PICKUP.md`, memory `benchmark-timeout-kill-methodology`).
**Scope of the *decision*:** the solver's internal `--timeout` semantics (C++). The
benchmark wrapper grace fix is separate and already being implemented.

---

## 1. The problem

The solver's `--timeout` is measured on **wall clock** —
`src/main/solver/solver.h:86` types `clock` as `std::chrono::high_resolution_clock`,
and `dfs()` stops when `clock::now() >= start + timeout`.

Under parallel load this makes results **load-dependent**: a process that is
time-sliced gets *less actual compute* per wall-second, so a wall-1000 ms budget buys
fewer searched states when the machine is busy than when idle. Two runs of the same
seed — one solo, one in a 16-wide batch — time out having done materially different
amounts of work. For a benchmarking tool whose entire job is fair comparison
(e.g. multiplicity vs LRU in `bench_multiplicity.sh`), that is a real methodology
hole: the cutoff is not comparable across machines, batch widths, or even time of day.

CPU time (user + system) measures *work the process actually did*, independent of how
the OS scheduled it. That is the load-invariant quantity we want a "time budget" to mean.

## 2. Evidence gathered (2026-05-30, this machine)

- Solver timer is wall (`high_resolution_clock`); wrapper (`perf_counter`) and
  `/usr/bin/time` "real" are also wall — **no clock-basis mismatch today**, all wall.
- A genuine 1000 ms-timeout run takes **1.13–1.37 s wall** idle (timeout + overhead).
- **Fixed per-run overhead is ~0.3–0.75 s wall but ~0 CPU**, dominated by the **default
  cache's mmap reservation** at startup/teardown (with a modest `--cache-capacity` it
  drops to ~0.01 s). This overhead is *outside* the search timer, so it is **not**
  fixed by changing the timer's clock — it is what the wrapper grace floor addresses.
  But it is the same root phenomenon (kernel/scheduling wall time ≠ CPU time) that
  makes wall-based budgets fragile under load.

So there are **two distinct issues**, do not conflate them:
1. **Wrapper grace** vs fixed startup/teardown overhead → fixed by the grace floor
   `timeout + max(0.5×timeout, 10 s)` (being implemented now).
2. **Solver budget clock** (this proposal) → wall vs CPU for the *search* cutoff.

## 3. The core tension a CPU-time budget creates

If the solver budgets on **CPU** but the wrapper polices on **wall**, they diverge
under load: reaching 1000 ms of CPU at a 25 % scheduler share takes ~4 s of wall.
A wall-based wrapper deadline would then fire and (now gracefully) terminate a run
that was behaving perfectly. So we cannot just switch the solver clock and leave the
wrapper as-is — they must be reconciled.

## 4. Options

**A. Status quo — wall.** Simplest. Keeps solver and wrapper on the same clock.
Cost: load-dependent, non-comparable cutoffs (the problem above).

**B. CPU-time budget, wall safety-cap in the solver (recommended).**
- Primary budget: **CPU time (user + system)** via `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)`
  (POSIX; works on Linux and macOS ≥10.12), checked at the same point as today
  (top of the DFS loop). The solver is single-threaded, so user+sys is unambiguous.
- The solver *also* keeps a **generous wall hard-stop** (e.g. `wall_cap = K ×
  cpu_timeout`, K configurable, default large) so it always self-terminates and emits
  a clean `TIMEOUT` even if badly descheduled — preserving the "solver self-reports,
  wrapper rarely intervenes" property.
- The wrapper grace must then exceed the wall hard-cap (e.g. grace keyed off
  `wall_cap`, not `cpu_timeout`).
- Pro: load-invariant work budget + still robust. Con: two budgets to reason about;
  `wall_cap` is a heuristic.

**C. Deterministic budget (states/operations), not time.**
- Cap on `states_searched` (or a node/operation counter) instead of any clock.
- Pro: **fully reproducible** — identical cutoff regardless of machine, load, or time;
  the gold standard for comparing algorithms. Con: changes the meaning of the knob
  (not "time"); a node budget must be chosen; wall/CPU time still wanted as a *reported
  metric* for "how fast", just not as the *cutoff*.
- Could complement B: budget by CPU time for "time-limited" experiments, and offer a
  `--max-states` cutoff for reproducible comparisons.

## 5. Recommendation

Adopt **B** (CPU-time primary budget + solver wall safety-cap), and additionally expose
**C**'s `--max-states` cutoff for experiments that need bit-for-bit reproducible cutoffs
(the multiplicity-vs-LRU comparison would benefit — equal node budgets remove timing
noise entirely). Keep wall time as a *reported* CSV metric regardless.

This keeps the property the kill-discipline work relies on — the solver almost always
self-terminates and emits clean JSON — while making the *budget* mean "work done," not
"wall elapsed."

## 6. What changes if adopted

- `solver.h`/`solver.cpp`: replace the wall deadline check with a CPU-time check
  (`CLOCK_PROCESS_CPUTIME_ID`) plus a wall hard-cap; thread `cpu_timeout` + `wall_cap`
  through `run()`. `command_line_helper`: `--timeout` becomes CPU ms; add `--wall-cap`
  (and optionally `--max-states`).
- `bench_lib/process.py`: grace keyed off the solver's *wall* cap, not the CPU budget.
- `csv_schema.md`: document that `timeout_ms` is now CPU ms; clarify `time_us` (wall).
- Regenerate oracles / re-baseline: **a CPU-time cutoff changes which instances time
  out**, so regression oracles and any saved baselines must be regenerated. This is the
  main cost and why it needs deliberate scheduling, not a snap change.

## 7. Open questions for Ian

1. **CPU = user only, or user + system?** (Recommend user+sys; the mmap/page-fault
   work that hurt us is `sys`.)
2. **Option B alone, or also add `--max-states` (C)** for reproducible comparisons?
3. **Wall safety-cap multiplier `K`** default (e.g. 10×)? It bounds the worst-case wall
   per run and sets how generous the wrapper grace must be.
4. **Migration:** regenerate all oracles/baselines as part of this, or gate it behind a
   flag (`--timeout-clock {wall,cpu}`, default wall) for a transition period?
