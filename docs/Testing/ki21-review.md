# KI-21 Review Report — Testing Rationalisation

**Branch:** `feature/testing-rationalisation` (off `dev`)
**Prepared for:** independent evaluation by senior model
**Date:** 2026-05-12

---

## What this branch does

Known Issue #21 flagged the testing infrastructure as having high cognitive
overhead: three build configs with distinct test sets, multiple Python scripts
with overlapping concerns, silent no-ops for trace tests in non-trace builds,
and no single entry point for new contributors.

This branch does not change what is tested. It makes the system navigable.

---

## Deliverables

### D1 — Clarify GTest structure (commit `c136ecd`)

**Files changed:** `CMakeLists.txt`, `CLAUDE.md`

**What changed and why:**

1. `unit_tests_full` target removed. It had `--gtest_filter=-DualCacheTest.*:MismatchDiagnostic.*`
   which excluded two test suites. Investigation showed `DualCacheTest` no longer
   exists in the codebase (removed in a previous refactor) and `MismatchDiagnostic`
   does not appear either. The filter was dead code silently skipping nothing.
   Running `unit_tests` without the filter passes — confirmed locally.

2. Added explanatory comment in `CMakeLists.txt` explaining that the `unit_tests`
   binary is always compiled with `SOLVITAIRE_SEARCH_TRACE=ON` (all three build
   configs), so `SearchTraceTest.*` and `SearchTraceAgreementTest.*` run everywhere.
   This is intentional — the GTest trace tests validate the trace *writer* logic;
   the CTest `trace_*` targets validate trace *identity* against a reference binary.
   These are different things with confusingly similar names.

3. Added a disabled CTest target `trace_tests_not_available` in non-trace builds.
   Previously `ctest -N` in a release or debug build silently showed no trace targets,
   making it look like they didn't exist. Now it shows a disabled placeholder with a
   message directing the user to run `./build.sh --trace`.

**Verification:** `ctest -N` in `cmake-build-release` should show
`trace_tests_not_available (Disabled)`. `ctest -R ^unit_tests$` passes in release.

---

### D2 — Consolidate trace scripts (commit `5d0c22f`)

**Files changed:** `scripts/compare_traces.py` (extended), `CMakeLists.txt` (2 targets),
`scripts/trace_regression.py` (deleted)

**What changed and why:**

`trace_regression.py` (316 lines) duplicated logic from both `compare_traces.py`
and `regression_runner.py`. The key function — `stream_compare()` — was actually
an improvement over `compare_traces.py`'s `compare_events()`: it reads one line at
a time from each file simultaneously (O(1) memory) rather than loading both traces
into RAM. For batch regression over 150–160 instances this matters.

A `--regression` mode was added to `compare_traces.py`:

```
python3 scripts/compare_traces.py --regression \
    --level {1|2} \
    --binary-a <ref-binary> \
    --binary-b <cur-binary> \
    --tests-dir <repo-tests-root>
    [--timeout-ms N] [--instances N] [--verbose]
```

Functions migrated from `trace_regression.py`:
- `stream_compare()` — streaming O(1) comparison engine (used by regression mode)
- `build_cmd()` — builds solver invocation from oracle entry
- `run_instance()` — runs ref+cur, compares traces, cleans up temp files
- `run_regression()` — main batch loop with progress reporting

The existing file/binary modes (`--full`, `--until-evict`, `--until-timeout`) and
their `read_events()`/`compare_events()` engine are unchanged.

The two CTest targets updated in `CMakeLists.txt`:
- `trace_regression_level1`: now calls `compare_traces.py --regression --level 1 --binary-a ... --binary-b ...`
- `trace_regression_level2`: same with `--level 2 --timeout-ms 5000`

`trace_regression.py` was deleted.

**Key design point:** `stream_compare()` stops at the first `TIMEOUT` event in
either trace — wall-clock speed may differ between a reference binary and a freshly
compiled binary, so the timeout boundary is excluded from comparison.

**Verification:** `compare_traces.py --help` shows `--regression` in the mode group.
The two CTest targets in `cmake-build-trace/CTestTestfile.cmake` now reference
`compare_traces.py`, not `trace_regression.py`.

---

### D3 — Unified test driver (commit `a808e5f`)

**Files created:** `scripts/run_tests.py` (191 lines, executable)

**What it does:**

A thin wrapper around `build.sh` + `ctest` that drives the 3-gate workflow.
Building is integral — each gate builds its own config before testing.

```
run_tests.py                    # all 3 gates, unit tests + level 1 regression
run_tests.py --gate release     # gate 1 only
run_tests.py --gate trace       # gate 2 only
run_tests.py --gate debug       # gate 3 only
run_tests.py --quick            # unit tests only, all 3 configs (~5 min)
run_tests.py --level 2          # unit tests + regression through level N (release gate)
run_tests.py --skip-build       # assume binaries already built
run_tests.py --dry-run          # print commands without executing
```

Gate definitions match `CLAUDE.md` exactly:
- Gate 1: `./build.sh --release --unit-tests` → `ctest -R ^unit_tests$` → `ctest -R regression_level1` (and levelN)
- Gate 2: `./build.sh --trace` → `ctest -R ^unit_tests$` → `ctest -R trace_`
- Gate 3: `./build.sh --debug --unit-tests` → `ctest -R ^unit_tests$`

Stops on first gate failure with a summary. Uses absolute paths derived from
`__file__` so it works from any working directory.

**Verification:** `python3 scripts/run_tests.py --dry-run` prints the correct
command sequence for all 3 gates. `--dry-run --quick` shows only `^unit_tests$`
steps. `--dry-run --gate release --level 2` shows level 1 and level 2 regression.

---

### D4 — Testing quick-start doc (commit `04b0770`)

**Files created:** `docs/testing-quickstart.md` (98 lines)
**Files changed:** `CLAUDE.md`

`docs/testing-quickstart.md` covers:
- TL;DR with `run_tests.py` invocations
- The 3-gate model in a table
- Decision tree: what to run after different kinds of code change
- GTest vs CTest distinction (the naming overlap is the main source of confusion)
- CTest targets at a glance (release + trace builds, with approximate run times)
- Pointer to `docs/regression_suite_guide.md` for adding tests
- Troubleshooting (wrong directory for ctest, missing reference binary,
  "trace tests not available" in non-trace build)

`CLAUDE.md` changes:
- Added `run_tests.py` usage snippet at the top of the Testing section, before the
  per-gate manual commands (which remain for reference)
- Fixed stale reference to `trace_regression.py` (deleted in D2) in the trace testing
  detail paragraph

---

## What was explicitly not changed

Per the plan's "What NOT to do":
- CTest target naming — unchanged
- `generate_baseline.py`, `curate_test_sets.py` — not touched (rare-use curation tools)
- The 3-build-config model — unchanged
- C++ test source files — not touched
- `regression_runner.py` — not touched

---

## Commit log

```
04b0770  docs: testing quickstart + CLAUDE.md update (KI-21 D4)
a808e5f  feat: add unified test driver scripts/run_tests.py (KI-21 D3)
5d0c22f  refactor: merge trace_regression.py into compare_traces.py (KI-21 D2)
c136ecd  refactor: clean up GTest structure and add trace no-op guard (KI-21 D1)
a52d04b  docs: lodge testing rationalisation plan (KI-21)
```

## Files to inspect

| File | Why |
|------|-----|
| `CMakeLists.txt` | D1 comments + disabled target; D2 CTest target commands |
| `scripts/compare_traces.py` | D2 merged `--regression` mode; `stream_compare()` at line ~130 |
| `scripts/run_tests.py` | D3 unified driver |
| `docs/testing-quickstart.md` | D4 quickstart |
| `CLAUDE.md` | D4 run_tests.py snippet + stale ref fix |
| `docs/Testing/PICKUP.md` | Branch status log |
