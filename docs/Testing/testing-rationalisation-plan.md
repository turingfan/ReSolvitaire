# Plan: Rationalise Testing Infrastructure (KI-21)

## Context

The testing infrastructure in ReSolvitaire has grown organically across multiple feature branches. It works correctly but has high cognitive overhead: three build configs with distinct test sets, multiple Python scripts with overlapping concerns, silent no-ops for trace tests in non-trace builds, and documentation spread across multiple files. A new contributor (human or AI) has no single entry point. This plan addresses Known Issue #21.

**Repo:** `ReSolvitaire-testing`
**Branch:** `feature/testing-rationalisation` (off `dev`)
**Goal:** make the testing system navigable without changing what it tests

---

## Deliverables (in implementation order)

### 1. Clarify GTest structure -- CMakeLists.txt and documentation

This is foundational work that affects everything else (the quickstart doc, the test driver's reporting, etc.).

The GTest setup has several confusing aspects:

**Problem A: `unit_tests` vs `unit_tests_full`.** The default `unit_tests` target uses `--gtest_filter=-DualCacheTest.*:MismatchDiagnostic.*` to exclude two test suites. DualCacheTest targets pre-templated-dispatch behaviour (M5); MismatchDiagnostic is a diagnostic tool. The exclusion isn't explained anywhere a developer would naturally look.

**Problem B: SearchTrace GTests run everywhere but CTest trace targets don't.** The `unit_tests` binary is *always* compiled with `SOLVITAIRE_SEARCH_TRACE`, so `SearchTraceTest.*` and `SearchTraceAgreementTest.*` (the metamorphic hash-only-vs-flat test) run in all three build configs. But the CTest-level trace targets (`trace_identity_*`, `trace_regression_*`) only exist in the trace build. This is actually correct behaviour -- the GTest trace tests validate the trace *writer* logic and cache agreement, while CTest trace targets validate trace *identity* against reference binaries. But the naming makes it look like trace testing only happens in the trace build.

**Problem C: Integration tests mixed into unit_tests.** The 13 per-game solvability tests (e.g. `KlondikeTest.SimpleSolvable`) are integration tests that invoke the full solver, but they live in the same `unit_tests` binary as genuine unit tests. No separation at CTest level.

**Proposed fixes:**
- Add clear comments in CMakeLists.txt explaining the `unit_tests`/`unit_tests_full` split and the gtest_filter rationale
- Investigate whether `unit_tests_full` is still needed -- run DualCacheTest to see if it passes on current dev. If it does, drop the filter and remove `unit_tests_full`. If not, document why it's excluded.
- Add a disabled CTest target for trace tests when not in trace build (the silent no-op fix)

### 2. Consolidate trace scripts -- merge `trace_regression.py` into `compare_traces.py`

`trace_regression.py` (316 lines) duplicates logic from both `compare_traces.py` and `regression_runner.py`:
- `stream_compare()` is a streaming version of `compare_events()` (better -- O(1) memory)
- `build_cmd()` duplicates oracle-entry-to-solver-args logic from `regression_runner.py`

Add a `--regression` mode to `compare_traces.py`:
```
python3 scripts/compare_traces.py --regression \
    --level 1 \
    --binary-a <ref-binary> \
    --binary-b <cur-binary> \
    --tests-dir tests
```

Specific changes:
- Adopt `stream_compare()` from `trace_regression.py` as the comparison engine for regression mode
- Keep `read_events()`+`compare_events()` for file/binary modes (they handle --until-evict and --until-timeout)
- Move `build_cmd()` and `run_instance()` into `compare_traces.py`
- Update 2 CTest targets in `CMakeLists.txt` (`trace_regression_level1`, `trace_regression_level2`)
- Delete `trace_regression.py`

### 3. Unified test driver -- `scripts/run_tests.py`

A single Python script that wraps the 3-gate workflow. Thin wrapper around `build.sh` + `ctest`.

```
scripts/run_tests.py                    # All 3 gates, unit tests + level 1
scripts/run_tests.py --gate release     # Gate 1 only
scripts/run_tests.py --gate trace       # Gate 2 only
scripts/run_tests.py --gate debug       # Gate 3 only
scripts/run_tests.py --quick            # Unit tests only, all 3 configs
scripts/run_tests.py --level 2          # Unit tests + up to level 2 regression
scripts/run_tests.py --skip-build       # Assume binaries already built
scripts/run_tests.py --dry-run          # Print what would run
```

Building is integral to testing -- the driver builds each gate's config automatically before running its tests. `--skip-build` is opt-in.

### 4. Test quick-start document -- `docs/testing-quickstart.md`

Short (under 100 lines) landing page for new contributors covering:
- TL;DR (run_tests.py)
- The 3-gate model
- What to run after a code change (decision tree)
- GTest vs CTest distinction
- CTest targets at a glance
- Adding a test (pointer to regression_suite_guide.md)
- Troubleshooting

Also update `CLAUDE.md` to reference the new driver and quickstart.

---

## What NOT to do

- Don't restructure CTest target naming -- the problem is discoverability, not naming
- Don't merge `generate_baseline.py` or `curate_test_sets.py` -- rare-use curation tools
- Don't add Make/task-runner on top of CMake
- Don't change the 3-build-config model
- Don't touch C++ test source files
- Don't split the unit_tests binary into separate unit/integration binaries

---

## Verification

1. GTest clarity: resolve or document `unit_tests_full` vs `unit_tests`. Verify disabled trace guard in release build.
2. Trace consolidation: `ctest -R trace_regression_level1` in trace build should produce same results via merged script.
3. run_tests.py: `--quick` builds all 3 configs and runs unit tests. `--dry-run` prints planned commands.
4. Quick-start doc: CTest target names match `ctest -N` output. GTest/CTest distinction is clear.
