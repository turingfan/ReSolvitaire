# dev Branch — Active Documentation Index

This is the navigation entry point for the current `dev` branch.
Files are not duplicated here — this index points to their canonical locations.

---

## Current State

The `dev` branch is the primary working branch (cross-platform macOS + Linux).
The most recent completed work is the variant-build fix and hash-only descriptor store
(known-issues #11 and #14, all 5 commits merged April 2026).

---

## Key Active Documents

### Issue Tracking
- [`docs/known-issues.md`](../known-issues.md) — open and resolved issues; start here

### Testing
- [`docs/regression_suite_guide.md`](../regression_suite_guide.md) — how to run levels 1–5 regression tests

### Next Planned Work
- [`docs/proposals/PROPOSAL-templated-game-state-dispatch.md`](../proposals/PROPOSAL-templated-game-state-dispatch.md) — templating `game_state` on cache policy (known-issues #8); deferred until after benchmarking

### Recently Completed Work (reference)
- [`docs/fix-variant-build-hash-only/PICKUP.md`](../fix-variant-build-hash-only/PICKUP.md) — branch summary, commit table, session log
- [`docs/fix-variant-build-hash-only/implementation_plan.md`](../fix-variant-build-hash-only/implementation_plan.md) — detailed per-commit plan and rationale

### Background / Architecture Narrative
- [`docs/cache-redesign/OVERVIEW.md`](../cache-redesign/OVERVIEW.md) — full narrative of the cache redesign effort
- [`docs/cache-redesign/flat_cache_branch_summary.md`](../cache-redesign/flat_cache_branch_summary.md) — flat cache benchmark results and design summary
