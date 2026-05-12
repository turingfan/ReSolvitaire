# PICKUP -- Testing Rationalisation (KI-21)

**Branch:** `feature/testing-rationalisation`
**Last updated:** 2026-05-12
**Status:** COMPLETE — ready to merge to dev

## What this branch did

Addressed Known Issue #21 (testing infrastructure complexity). Four deliverables, all complete:

1. **GTest structure clarification** — removed stale `--gtest_filter` and `unit_tests_full` (excluded tests no longer exist); added explanatory comments; added disabled `trace_tests_not_available` guard in non-trace builds
2. **Trace script consolidation** — merged `trace_regression.py` into `compare_traces.py` (`--regression` mode); updated CTest targets; deleted `trace_regression.py`
3. **Unified test driver** — `scripts/run_tests.py` wraps the 3-gate workflow; builds automatically before testing; supports `--gate`, `--quick`, `--level N`, `--skip-build`, `--dry-run`
4. **Quick-start document** — `docs/testing-quickstart.md` (98 lines); updated `CLAUDE.md` with `run_tests.py` usage and pointer to quickstart

## Verification

- `unit_tests` passes in release build (106s)
- `regression_level1` passes (6s)
- `run_tests.py --dry-run` produces correct command sequences for all flag combinations
- `ctest -N` in release build shows `trace_tests_not_available (Disabled)`

## Post-merge

Archive this PICKUP and the plan to `01-Knowledge-Base/Archive/testing-rationalisation/`.
