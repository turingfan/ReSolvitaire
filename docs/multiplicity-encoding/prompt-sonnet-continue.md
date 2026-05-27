You are continuing work on the ReSolvitaire multiplicity-encoding branch.

## Before you do anything

1. Read `01-Knowledge-Base/AGENTS.md` — mandatory rules for all agents.
2. Read the branch PICKUP at `02-Code-Repositories/claude-ReSolvitaire/docs/multiplicity-encoding/PICKUP.md`.
3. Read the implementation plan at `02-Code-Repositories/claude-ReSolvitaire/docs/multiplicity-encoding/implementation-plan.md`.
4. Read `02-Code-Repositories/claude-ReSolvitaire/CLAUDE.md` — build/test commands.

## What has been done

- Stage 0.1 (audit) and Stage 0.2 (extract descriptor engine) are complete and committed on the `multiplicity-encoding` branch.
- Stage 0.2 extracted all descriptor/hash logic into `flat_descriptor_engine.h`, a template class used by FlatPolicy, HashOnlyPolicy, and PredecessorPolicy. LRUPolicy uses a no-op `null_descriptor_engine`.
- All three test gates pass (release, debug, trace unit tests + level 1 regression across all 4 variant binaries).

## Your task

Work through the remaining stages of the implementation plan **one stage at a time**, in order. The next stage is **Stage 0.3** (validate PredecessorPolicy compatibility), then **Stage 1** (from-scratch multiplicity, no symmetry).

For each stage:
1. Read the plan section carefully before writing any code.
2. If you encounter a semantic/domain question (game rules, descriptor meanings, architectural decisions), STOP and ask Ian. Do not guess.
3. If you encounter a bug, STOP and report the symptom. Do not investigate or fix.
4. Build and run all three test gates before considering a stage complete. At minimum: release unit tests + level 1 regression, debug unit tests, trace unit tests.
5. Commit completed work with a clear message. One commit per stage unless a stage naturally splits.
6. Update `docs/multiplicity-encoding/PICKUP.md` after each commit to reflect current state.
7. Push after each stage.

## Important constraints

- The multiplicity cache is a NEW policy alongside existing ones, not a replacement. Existing policies must continue to work identically.
- Stage 1 uses from-scratch recomputation (dirty flag + lazy recompute), not incremental updates. Keep it simple.
- Stage 1 is opt-in via `--cache-type multiplicity` CLI option.
- The MLD_/MPD_ naming convention is documented in the plan — use it.
- Do not proceed to Stage 2 (symmetry) until Stage 1 is complete and validated.
- Do not proceed past Stage 2 without explicit approval from Ian.

## Working directory

`02-Code-Repositories/claude-ReSolvitaire` on branch `multiplicity-encoding`.
