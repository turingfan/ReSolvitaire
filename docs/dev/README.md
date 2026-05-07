# dev Branch — Active Documentation Index

This is the navigation entry point for the current `dev` branch.
Files are not duplicated here — this index points to their canonical locations.

---

## Current State

The `dev` branch is the primary working branch (cross-platform macOS + Linux).

Most recently merged: `feature/search-trace` (2026-05-07) — search trace
infrastructure for pre-merge validation of `feature/templated-dispatch`.

In progress: `feature/templated-dispatch` — templates `game_state` and `solver`
on cache policy, eliminating runtime dispatch. Trace validation against the
`feature/search-trace` reference binaries passed on both macOS and Linux.
Merge to `dev` deferred for final decision.

---

## Key Active Documents

### Issue Tracking
- [`docs/known-issues.md`](../known-issues.md) — open and resolved issues; start here

### Testing
- [`docs/regression_suite_guide.md`](../regression_suite_guide.md) — how to run levels 1–5 regression tests
- `CLAUDE.md` (root) — all three build gates (release, trace, debug) and testing commands

### Next Planned Work
- `feature/templated-dispatch` — awaiting merge decision; branch + trace validation complete

### Recently Completed Work (archived to 01-Knowledge-Base/Archive/)
- `search-trace/` — search trace infrastructure (merged 2026-05-07)
- `templated-dispatch/` — phase A planning and design (work ongoing on branch)
- `fix-variant-build-hash-only/` — variant build fix and hash-only descriptor store

### Background / Architecture Narrative
- [`docs/cache-redesign/OVERVIEW.md`](../cache-redesign/OVERVIEW.md) — full narrative of the cache redesign effort
- [`docs/cache-redesign/flat_cache_branch_summary.md`](../cache-redesign/flat_cache_branch_summary.md) — flat cache benchmark results and design summary
