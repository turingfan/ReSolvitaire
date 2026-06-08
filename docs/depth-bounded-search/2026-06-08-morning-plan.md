# Plan (2026-06-08) — Stage-2b blocker decisions + light cleanup

**Status: APPROVED by Ian and EXECUTED 2026-06-08.** Durable in-repo copy of the approved
plan (the working copy lived in the ephemeral `~/.claude/plans/`). Decisions are the
canonical record in [`BLOCKERS.md`](BLOCKERS.md); this is the point-in-time plan.

## Context
After the overnight night-shift, Stage 1 (PR1 `1a–1d` + PR2 `1e–1f`), Stage 2a (inert
cache-format), and 2d (GHI/cycle adversarial tests) are landed, independently verified, and
pushed (`a15b023`; **M2** reached, **M3**-ready). The three soundness questions blocking
Stage 2b were queued in `BLOCKERS.md`. Ian reviewed the work and **resolved B1/B2/B3**, and
asked for no extensive coding today — so this plan only **records the decisions** and does
**light cleanup**; it does **not** implement 2b/2c (now unblocked, deferred).

## Decisions (Ian, 2026-06-08)
- **B1 = Option A.** Forced *uncached* dominance/K+ edge is **pass-through**: contributes
  `1 + child_b` to its nearest **cached** ancestor; the dominance state stays uncached.
  Uniform ply-metric (every move = 1 ply; a K+ stock move = 1, not its physical card-deals).
  *2b trap:* fold the uncached edge's value **upward**; never key finalisation on the absent
  cache iterator (silent drop ⇒ false `unwinnable`).
  *Correction:* Option C (charge 0) is actually **sound** (under-counts `b` ⇒ more conservative,
  never a false prune); A preferred for accurate budgets + metric consistency with PR1.
- **B2 = Option A.** `DEAD` is a **soft pin** — evict before throwing `MEM_LIMIT`; `MEM_LIMIT`
  fires only when all remaining entries are `live`.
- **B3 = `live`-bit per-pass cycle detection; no separate set.** Cycle detection is intra-pass
  only (a new pass is a fresh search; only the cache's reuse info crosses passes). Guarantee no
  stale `live` bit survives a pass (zero-on-exit or generation stamp — implementer's choice).
- **F1 = DEFERRED** — Ian to revisit (doesn't fully understand it yet); best walked through
  while building 2b. Not a blocker.

## Cleanup done (docs/housekeeping only — no `src/main` changes)
1. Committed the terminal docs (`PICKUP.md`, `progress-log.md`).
2. `BLOCKERS.md` — B1/B2/B3 marked RESOLVED; C-soundness mislabel fixed; F1 marked DEFERRED;
   stray double `---` removed.
3. `PICKUP.md` + `progress-log.md` — decisions recorded; 2b UNBLOCKED-but-deferred; next-session
   prompt points at `HANDOFF.md`.
4. Added `HANDOFF.md` (control doc for a fresh lead).
5. Orphaned branches (`claude/ecstatic-hopper-tpykG`, `…-wip-backup`) documented for manual
   deletion (web git proxy 403s on deletion; no MCP branch-delete tool).

## Out of scope today
Stage 2b/2c implementation — the soundness-critical heart. Unblocked by the decisions above;
a fresh focused session does it (see `HANDOFF.md`).
