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

### Gate 1 baseline + Stage 0 sweep — done

- **Gate 1 baseline GREEN** (clean starting point before any code change):
  release `unit_tests` pass (120 s); `regression_level1` + flat/hash_only/lru
  variants pass (27 s). Gate 2 (trace) deferred — needs reference binaries.
- **Stage 0 sweep complete.** 4 games × seeds 1–50, 6 s cap, run as 4 parallel
  background jobs (mechanical CLI loops — chose background Bash over LLM subagents
  as the simpler tool for a pure measurement sweep). Raw CSVs committed to
  `stage0-data/`; analysis via `stage0_analyze.py`.
- **Result → GO (recommended).** See [`stage0-report.md`](stage0-report.md).
  Headline: snake pathology real in 3/4 games; Beleaguered Castle (the JAIR
  target) shows shallow unwinnable proofs (`max_depth` ≤186) and a **230× gap**
  between resolved (median 1 252) and timeout (median 288 530) depths. Honest
  caveat: free-cell/spanish-patience are *broadly* deep (depth not a thin-tail
  artifact) — whether their deep timeouts collapse is the question Stage 1 settles.
- **Awaiting Ian: M1 go/no-go** before writing any Stage 1 code.

---

## 2026-06-07 — M1 GO; trace identity gate built (own x86_64 reference)

- **Ian gave M1 GO** ("build Stage 1") and chose **self-baseline** for the identity
  gate. Follow-up: the committed trace reference is **Linux ARM64** (`…-linux-arm64-…`)
  — wrong arch for this x86_64 web container — so Ian authorised **building our own
  reference from the current pre-change state before any code change.**
- **Reference built (pristine).** Confirmed zero diff in `src/`+`CMakeLists.txt` at
  HEAD `45ccd43`, then `./build.sh --trace` → snapshotted `solvitaire-trace` to
  **`/home/user/reference-bin/solvitaire-trace-ref-45ccd43`** (SHA1 `894bcbb…`).
  Kept **session-local, not committed**: `AGENTS.md`/`01-KB` are absent from this
  checkout so the authoritative "where binaries go" rule is unverifiable, and the
  visible convention (CMake default → external `05-Executables/`; `.gitignore` skips
  `cmake-build-*`) says binaries live outside the solver repo. Offered to commit it
  in-repo if Ian prefers (1-min change).
- **Reproducer (any future session regenerates the identical reference):**
  `git checkout 45ccd43 && ./build.sh --trace` → `cmake-build-trace/bin/solvitaire-trace`.
  Wire the gate with `cmake -DTRACE_REF_BIN=<that binary> cmake-build-trace`.
- **Identity gate validated on x86_64 (candidate == reference now → must pass):**
  - `trace_identity_flat` / `trace_identity_lru` / `trace_until_timeout` — PASS (determinism).
  - `SearchTraceTest.*` + `SearchTraceAgreementTest.*` (5 tests) — PASS (incl. the
    107 s HashOnlyVsFlat 50-seed agreement test).
  - **`trace_regression_level1` = 150/150 instances, every event matched** (up to
    212 591 events/instance; 41 s direct run). This is the real `L=∞` identity gate:
    after Stage 1, the bound-disabled rebuild must still be 150/150 to prove
    byte-identical search.
- **Stage 1 PR1 (items 1a–1d) dispatched** to a background implementer subagent
  (CLI bound flags + depth cut + `BOUNDED_EXHAUSTED` + result mapping), self-checked
  against the identity gate + release gates; orchestrator verifies independently
  before it's blessed. Outer ID loop (1e) + differential harness (1f) deferred to PR2.

