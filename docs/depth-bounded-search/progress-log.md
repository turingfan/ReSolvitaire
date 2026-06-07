# Progress Log — depth-bounded-search

Append-only. Newest entries at the bottom. One block per session/work-chunk.

---

## 2026-06-07 — M0 approved; Stage 0 kickoff

- Ian approved the implementation plan and **made the assistant lead**, with
  defaults D1–D6 accepted (revisit in hindsight if needed). `docs/depth-bounded-search/`
  confirmed as the official system of record. Reference-binary / `01-KB` repo
  dependencies: Ian will provide when they become important (flag at Stage 1).
- Phase now: **Stage 0 (measurement, no algorithm change).**
- First action: establish a clean **baseline build + gate run** in this fresh
  container before changing any code (so later breakage is attributable), and
  recon the existing instrumentation counters to spec items 0a/0b precisely.

### Groundwork done this session

- **Environment gap found + fixed (ephemeral).** Fresh container had **no Boost
  dev headers** (`find_package(Boost program_options)` would fail); only runtime
  libs present. OS is Ubuntu 24.04/`noble` (Dockerfile targets 22.04). Ubuntu
  archive reachable (two unrelated third-party PPAs 403). Installed
  `libboost-program-options-dev` (+`time`) → Boost 1.83 headers present, cmake now
  finds Boost. **This install does not survive container reclaim** → recommend a
  SessionStart setup hook so every web session has it (flagged to Ian).
- `build/`, root `solvitaire`, root `unit-tests` are **committed artifacts**
  (git-tracked), not built here. Fresh `./build.sh --release --unit-tests` run
  into `cmake-build-release/` — `solvitaire` builds clean; `unit_tests`/variants
  compiling.
- **Trace reference binaries absent** (`05-Executables/reference/` missing) —
  Stage 1 trace-gate dependency; Ian to provide when we reach it.
- **Stage 0 item 0a needs NO code:** the JSON already emits `final_depth`,
  `max_depth`, and `solver_resident_bytes` (peak RSS via `getrusage`, `main.cpp`
  ~385). For unsolvable, proof depth = `max_depth` (final_depth returns to 0).
- **Measurement pipeline validated** on `cmake-build-release/bin/solvitaire`
  (klondike seeds 1–3): e.g. seed1 unsolvable max_depth=23 (158 k states); seed2
  winnable depth=91; seed3 winnable depth=90, max_depth=92 (928 k states).
  Klondike is shallow → the deep tail is game-specific (hunt Beleaguered Castle
  et al. in the sweep).
- **Reuse, not reinvent:** Stage-0 sweep will use the existing
  `--random/--type/--json/--timeout` interface (as `regression_runner.py` /
  `run_benchmark.py` already do).

### Next (resumes on build completion)

1. Gate 1 baseline (release `unit_tests` + `regression_level1`) — establish green
   baseline before any code change. (Gate 2/trace deferred: needs reference binaries.)
2. Stage 0 sweep across games + seeds (deep-tail focus) → `stage0-report.md` → M1
   go/no-go. Parallel measurement-runner subagents per game.

