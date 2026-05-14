# dev Branch — Active Documentation Index

This is the navigation entry point for the current `dev` branch.
Files are not duplicated here — this index points to their canonical locations.

---

## Current State

The `dev` branch is the primary working branch (cross-platform macOS + Linux).

Most recently landed (2026-05-12):
- **`feature/testing-rationalisation`** — KI-21: unified test driver (`run_tests.py`),
  trace script consolidation (`compare_traces.py --regression`), GTest cleanup,
  testing quickstart doc, comprehensive testing guide.

Previously landed (2026-05-07):
- **`feature/templated-dispatch`** — `game_state` and `solver` templated on cache
  policy (`game_state_impl<Policy>`, `solver_impl<Policy>`). Legacy concrete caches
  (`flat_cache`, `hash_only_cache`, `predecessor_flat_cache`, `dual_cache`,
  `cache_factory`) removed. Byte-identical search behaviour verified via trace
  regression against reference binaries on macOS and Linux.
- **`feature/search-trace`** — search trace infrastructure (`search_trace.h/cpp`,
  `STRACE_*` callsites, `compare_traces.py`, CTest targets).

Previous dev state (old concrete-cache system) preserved on branch `cache-v1`.

---

## Key Active Documents

### Issue Tracking
- [`docs/known-issues.md`](../known-issues.md) — open and resolved issues; start here

### Testing
- [`docs/testing-quickstart.md`](../testing-quickstart.md) — quick-start guide for new contributors
- [`docs/testing-guide.md`](../testing-guide.md) — comprehensive testing reference (regression, trace, containers)
- `CLAUDE.md` (root) — all three build gates (release, trace, debug) and testing commands

### Next Planned Work
- Benchmarking: compare templated-dispatch vs old concrete-cache system
  (use `cache-v1` branch as baseline)

### Recently Completed Work (archived to 01-Knowledge-Base/Archive/)
- `testing-rationalisation/` — KI-21: unified test driver, trace consolidation, docs
- `templated-dispatch/` — full phase A plan, design decisions, session logs
- `search-trace/` — search trace infrastructure design and implementation plan
- `fix-variant-build-hash-only/` — variant build fix and hash-only descriptor store

### Background / Architecture Narrative
- [`docs/cache-redesign/OVERVIEW.md`](../cache-redesign/OVERVIEW.md) — full narrative of the cache redesign effort
- [`docs/cache-redesign/flat_cache_branch_summary.md`](../cache-redesign/flat_cache_branch_summary.md) — flat cache benchmark results and design summary
