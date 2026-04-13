# Phase 2 + Phase 3 Workflow

**Date written:** 2026-04-13
**Current state:** `feature/pile-first-undo` holds completed Phase 1 (tip `4cca382`).
**Covers:** branch strategy, session cadence, PICKUP protocol, merge gates.

---

## 1. Branch Strategy (starting from end of Phase 1)

```
dev (integration, always green)
 │
 ├─ (step 1) merge feature/pile-first-undo  ──► dev@P1  [Phase 1 landed]
 │
 ├─ feature/template-cache                  ← branched from dev@P1
 │     │
 │     ├─ P2-A  add generic_flat_cache.h + policies + static_asserts
 │     ├─ P2-B  unit tests for each specialisation
 │     ├─ P2-C  wire cache_factory behind USE_GENERIC_CACHE
 │     ├─ P2-D  DualCache parity harness (non-accordion)
 │     └─ P2-E  regression L1+L2 with USE_GENERIC_CACHE=ON
 │
 ├─ (step 2) merge feature/template-cache   ──► dev@P1+P2
 │
 └─ feature/conditional-compilation         ← branched from dev@P1+P2
       │
       ├─ P3-A  SOLVITAIRE_COMPUTES_FLAT_HASH guards
       ├─ P3-B  CMake variant targets + factory dispatch
       ├─ P3-C  ineligible-rules error handling
       ├─ P3-D  compare_binaries.sh
       └─ P3-E  per-variant regression L1
```

### Why this, specifically

1. **Merge Phase 1 to `dev` first, before starting Phase 2.** The strategy doc says "Phase 2 from dev," and doing the merge first means Phase 2 branches from a clean, Phase-1-inclusive base. No rebasing later, no duplicated commits.

2. **Phase 2 does NOT branch from `feature/pile-first-undo`.** File overlap is zero (Phase 1 touched `game_state.*` + `CMakeLists.txt` option removal; Phase 2 touches `generic_flat_cache.h` + `cache_factory.h` wiring). Branching from `dev` post-merge gives the same code but a reviewable diff that is pure Phase 2.

3. **Phase 3 does NOT run in parallel with Phase 2.** Phase 3's `SOLVITAIRE_LRU_ONLY` binary strips Zobrist updates, which only makes sense once the template cache (Phase 2) is in place. Sequential, not parallel.

4. **No long-lived `phase-2-3` umbrella branch.** Each phase lands to `dev` on its own PR. A merged phase is one atomic checkpoint; if Phase 3 explodes, Phase 2 is still landed and benchmarkable.

### Concrete git operations (Claude runs these, with express permission per step)

Each of the five checkpoints below is a risky, shared-state operation (merge to `dev`, push to origin, branch creation). Claude executes them via Bash, but **only after Ian explicitly says "go" for that specific checkpoint** — blanket pre-approval does not carry across checkpoints. At each gate Claude:

1. States the exact commands about to run.
2. States the precondition (tests green, PICKUP updated, Ian's sign-off on the diff).
3. Waits for "yes / go / proceed" from Ian for that checkpoint.
4. Runs the commands and reports the result.

```bash
# Checkpoint 1: land Phase 1 — requires explicit "go"
git checkout dev
git pull
git merge --no-ff feature/pile-first-undo -m "Phase 1: pile-first undo + eliminate zobrist_undo_stack"
git push

# Checkpoint 2: start Phase 2 — requires explicit "go"
git checkout -b feature/template-cache dev
git push -u origin feature/template-cache

# ... Phase 2 commits P2-A through P2-E (each is its own session, each is its own commit approval) ...

# Checkpoint 3: land Phase 2 — requires explicit "go"
git checkout dev
git merge --no-ff feature/template-cache -m "Phase 2: template cache unification"
git push

# Checkpoint 4: start Phase 3 — requires explicit "go"
git checkout -b feature/conditional-compilation dev
git push -u origin feature/conditional-compilation

# ... Phase 3 commits P3-A through P3-E ...

# Checkpoint 5: land Phase 3 — requires explicit "go"
git checkout dev
git merge --no-ff feature/conditional-compilation -m "Phase 3: conditional compilation variant binaries"
git push
```

**Permission model (overrides default CLAUDE.md caution):** Ian authorises Claude to run `git merge --no-ff`, `git push`, and `git checkout -b` for the five checkpoints above, on a per-checkpoint basis. Each individual commit inside a phase is still approved separately per the normal "one commit per session" rhythm. Destructive operations (`git reset --hard`, force-push, branch deletion) are **not** covered by this authorisation and still require a fresh explicit ask.

---

## 2. Session Cadence

Each session = exactly one commit from the active plan. This is the Phase 1 rhythm that worked; keep it.

| Session | Scope | Duration |
|---|---|---|
| Kickoff | Read PICKUP, confirm branch, implement one commit, run tests, update PICKUP | ~1 context window |

Rules inherited from Phase 1 (CLAUDE.md Process Rules):

1. **Bug encountered → stop and report.** Never investigate silently.
2. **Semantic question → stop and ask Ian.** Don't reason your way to an answer.
3. **Scope → one named commit per session.** No commit drift.
4. **Test failure → bug report, not debugging task.**

New rule for Phase 2/3 specifically:

5. **KI-7 accordion failures are expected.** Do NOT investigate. If a Phase 2 test surfaces an accordion failure that matches the existing KI-7 pattern, note it in PICKUP and move on.

### End-of-session protocol

1. Run the commit's validation checklist (see PICKUP for current commit).
2. If green: write the commit, update PICKUP to mark the commit done and point at the next one, confirm with Ian.
3. If red: stop, write the symptom in PICKUP under "Current Blocker," ask Ian.

---

## 3. PICKUP Protocol

One PICKUP per phase. Phase 2 gets `docs/refactoring/PICKUP-phase2.md` (the initial version is written as part of this same change). Phase 3 gets `PICKUP-phase3.md`, written at the moment Phase 2 lands — not before, because by then we may know things that change the Phase 3 plan.

At end of phase, the per-phase PICKUP is archived (renamed to `PICKUP-phase2-final.md` or similar) and the current `PICKUP.md` symlink/pointer moves to the next phase's document.

Current PICKUP states:

- `docs/refactoring/PICKUP.md` — Phase 1 complete (current, do not delete until Phase 2 starts)
- `docs/refactoring/PICKUP-phase2.md` — Phase 2 initial (written now, empty commit log)
- `docs/refactoring/PICKUP-phase3.md` — to be written at Phase 2 landing

Each PICKUP contains:

- Current commit and what the next session should do first
- Commits done so far (one line each)
- Test status (what passes, what's expected to fail and why — KI references)
- Open known issues
- Exact build and test commands for this phase
- Current blocker (if any) — empty when green

---

## 4. Merge Gates

No merge to `dev` unless **all** of the following:

- `ctest -R unit_tests` passes (release build)
- `ctest -R regression_level1` passes (release build)
- The phase's success criteria in `phaseN_plan.md` are ticked off in PICKUP
- Ian has reviewed the diff and signed off

Gates specific to each phase are listed in the respective plan documents (`phase2_plan.md` §Success Criteria, `phase3_plan.md` §Success Criteria).

Two pre-existing known failures (KI-3 `BlackHoleUsesNewCache`, KI-7 `AccordionAgreement`) remain ignored through Phase 2 and Phase 3. They do not block merges.

---

## 5. What we already decided and won't re-litigate

- Accordion / predecessor cache correctness is out of scope until a later phase.
- 2-deck games are out of scope (they use LRU anyway).
- Memory-usage verification is deferred.
- Performance benchmarking is deferred — equivalence, not speed, is the Phase 2/3 goal.
- Legacy tagging (execution_strategy.md Phase 4) is already handled by the existing `pre-refactor-work` tag at `a784c7b`; no new Phase 4 work is needed.
- Phase 5 (cleanup/deletion of original caches) is not scheduled yet — it happens after Phase 3 lands and benchmarks confirm the variants are worth keeping.
