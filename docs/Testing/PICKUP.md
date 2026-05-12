# PICKUP -- Testing Rationalisation (KI-21)

**Branch:** `feature/testing-rationalisation`
**Last updated:** 2026-05-12
**Status:** All deliverables complete

## What this branch does

Addresses Known Issue #21 (testing infrastructure complexity). Four deliverables:

1. Clarify GTest structure (CMakeLists.txt comments, unit_tests_full investigation, silent no-op fix)
2. Consolidate trace scripts (merge trace_regression.py into compare_traces.py)
3. Unified test driver (scripts/run_tests.py)
4. Test quick-start document (docs/testing-quickstart.md + CLAUDE.md update)

See `docs/Testing/testing-rationalisation-plan.md` for full plan.

## Current state

- [x] Plan written and committed
- [x] Deliverable 1: GTest structure clarification
  - Removed stale `--gtest_filter` (DualCacheTest/MismatchDiagnostic don't exist in code)
  - Removed `unit_tests_full` target (identical to `unit_tests` now)
  - Added explanatory comments about unit_tests binary (always has SOLVITAIRE_SEARCH_TRACE)
  - Added disabled `trace_tests_not_available` guard in non-trace builds
  - Updated CLAUDE.md to remove `unit_tests_full` reference
  - Verified: `unit_tests` passes in release build (106s)
- [x] Deliverable 2: Trace script consolidation
  - Added `--regression` mode to `compare_traces.py` (stream_compare, build_cmd, run_instance, run_regression)
  - Updated 2 CTest targets in CMakeLists.txt to call compare_traces.py --regression
  - Deleted `scripts/trace_regression.py`
  - Verified: unit_tests passes in release build (106s)
- [x] Deliverable 3: Unified test driver
  - Created `scripts/run_tests.py` wrapping the 3-gate workflow
  - Supports --gate, --quick, --level N, --skip-build, --dry-run
  - Verified: dry-run output correct for all flag combinations; unit_tests still pass
- [x] Deliverable 4: Quick-start document
  - Created `docs/testing-quickstart.md` (98 lines)
  - Updated CLAUDE.md: added run_tests.py snippet, fixed stale trace_regression.py reference
  - Verified: unit_tests still pass in release

## What to do next

Branch is complete. Merge `feature/testing-rationalisation` into `dev`.
