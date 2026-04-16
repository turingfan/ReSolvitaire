# Refactoring Documentation

Canonical home for the caching-refactor plan and its per-phase working documents.

## Current (active)

| File | Purpose |
|---|---|
| `execution_strategy.md` | Master strategy covering all phases. Background reference; rarely edited. |
| `phase3_plan.md` | Phase 3 (conditional compilation) — detailed plan. |
| `phase2_3_workflow.md` | Branch strategy, session cadence, PICKUP protocol, merge gates. |
| `PICKUP-phase3.md` | Phase 3 working pickup. Updated at the end of every session. |

## `archive/`

Historical documents from completed or superseded phases. Kept for traceability; not consulted during active work unless investigating a regression.

| File | Status |
|---|---|
| `phase0_plan.md` | Phase 0 (metamorphic testing infra) — complete |
| `phase1_plan.md` | Phase 1 (pile-first undo) — complete |
| `phase2_plan.md` | Phase 2 (template cache unification) — complete |
| `PICKUP-phase2-final.md` | Phase 2 final pickup — all P2-A through P2-E done |
| `descriptor_undo_analysis.md` | Analysis that underpinned the Phase 1 descriptor recovery logic — implemented |
| `zobrist_undo_simplification.md` | Original Phase 1 proposal — implemented |
| `PROMPT-2026-04-11-1638.md` | Historical session prompt |
| `PROMPT-2026-04-13-CommitC.md` | Historical session prompt (Phase 1 Commit C) |
| `PROMPT-2026-04-13-CommitD.md` | Historical session prompt (Phase 1 Commit D) |
| `PROMPT-2026-04-14-P2-A.md` | Historical session prompt (Phase 2 Commit A) |

## Conventions

- Each phase has exactly one plan document (`phaseN_plan.md`) and one pickup (`PICKUP-phaseN.md`).
- Pickups live at the top level while their phase is active, then move to `archive/` as `PICKUP-phaseN-final.md` when the phase lands on `dev`.
- Plans do not move to archive — completed plans stay in `archive/` alongside their final pickup so the historical record is in one place.
- Session-specific prompts (`PROMPT-*.md`) are historical artifacts; they move to `archive/` as soon as their commit lands.
