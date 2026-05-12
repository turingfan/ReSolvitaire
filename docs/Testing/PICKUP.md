# PICKUP -- Testing Rationalisation (KI-21)

**Branch:** `feature/testing-rationalisation`
**Last updated:** 2026-05-12
**Status:** Plan lodged; implementation not yet started

## What this branch does

Addresses Known Issue #21 (testing infrastructure complexity). Four deliverables:

1. Clarify GTest structure (CMakeLists.txt comments, unit_tests_full investigation, silent no-op fix)
2. Consolidate trace scripts (merge trace_regression.py into compare_traces.py)
3. Unified test driver (scripts/run_tests.py)
4. Test quick-start document (docs/testing-quickstart.md + CLAUDE.md update)

See `docs/Testing/testing-rationalisation-plan.md` for full plan.

## Current state

- [x] Plan written and committed
- [ ] Deliverable 1: GTest structure clarification
- [ ] Deliverable 2: Trace script consolidation
- [ ] Deliverable 3: Unified test driver
- [ ] Deliverable 4: Quick-start document

## What to do next

Start Deliverable 1: investigate whether DualCacheTest passes on current dev, then update CMakeLists.txt with comments and the silent no-op guard.
