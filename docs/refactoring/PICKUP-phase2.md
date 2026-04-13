# Phase 2 Status: Template Cache Unification — Pickup Document

**Date written:** 2026-04-13
**Branch:** `feature/template-cache` (to be created from `dev` after Phase 1 merges)
**Plan:** `docs/refactoring/phase2_plan.md`
**Workflow:** `docs/refactoring/phase2_3_workflow.md`
**Status:** Phase 2 **not started**. Awaiting Phase 1 merge to `dev` and branch creation.

---

## PROCESS RULES — READ FIRST

1. **Bug encountered → STOP and report to Ian.** Do not investigate. Do not attempt a fix. Write the symptom in one paragraph under "Current Blocker" below and ask how to proceed.
2. **Semantic question → STOP and ask Ian.** He is the domain expert on descriptors, cache semantics, and correct behaviour.
3. **Scope → exactly one named commit per session.** Do not proceed to the next commit without Ian's explicit instruction.
4. **Test failure → a bug report, not a debugging task.**
5. **KI-7 accordion failures are EXPECTED. Do NOT investigate accordion.** If a test surfaces accordion breakage, note it and move on.

---

## What We Are Trying to Do

Introduce a single template `generic_flat_cache<Policy>` that subsumes `flat_cache`, `hash_only_cache`, and `predecessor_flat_cache`. The three originals are **left untouched** in Phase 2 — they stay in the tree as the reference implementations until Phase 5. The new template is added alongside them and wired into `cache_factory.h` behind a new CMake option `USE_GENERIC_CACHE` (default OFF).

The full plan with policies, cluster-size static_asserts, and success criteria is in `phase2_plan.md`. Read it before starting any commit.

---

## Prerequisites Before Session 1

**Claude runs these, each gated on Ian's explicit "go" for that step** (see `phase2_3_workflow.md` §"Concrete git operations"):

1. **Checkpoint 1 — Land Phase 1 to `dev`.** Claude states the merge commands, waits for Ian's "go," then runs:
   ```bash
   git checkout dev && git pull
   git merge --no-ff feature/pile-first-undo -m "Phase 1: pile-first undo + eliminate zobrist_undo_stack"
   git push
   ```
2. **Checkpoint 2 — Create `feature/template-cache` from `dev`.** Claude states the command, waits for Ian's "go," then runs:
   ```bash
   git checkout -b feature/template-cache dev
   git push -u origin feature/template-cache
   ```
3. **Baseline confirmation (no approval needed, local build only):**
   ```bash
   ./build.sh --release --unit-tests
   cd cmake-build-release && ctest -R unit_tests --output-on-failure
   # Expected: two pre-existing known failures only (KI-3, KI-7). Everything else passes.
   ```
   If baseline is not clean, stop and report — do NOT proceed to P2-A.

Once Checkpoints 1 and 2 are green and the baseline is confirmed, Session 1 starts on commit P2-A.

---

## Commits Planned (none done yet)

| ID | Title | Key files | Status |
|---|---|---|---|
| P2-A | Add `generic_flat_cache.h` + 3 policies + static_asserts | `src/main/game/generic_flat_cache.h` (new), `generic_flat_cache_policies.h` (new) | TODO |
| P2-B | Unit tests for each specialisation | `src/test/unit_tests/generic_flat_cache_test.cpp` (new) | TODO |
| P2-C | Wire `cache_factory.h` behind `USE_GENERIC_CACHE` | `cache_factory.h`, `CMakeLists.txt` | TODO |
| P2-D | DualCache parity harness (non-accordion only) | `src/test/unit_tests/generic_flat_dual_cache_test.cpp` (new) | TODO |
| P2-E | Regression L1 + L2 under `USE_GENERIC_CACHE=ON` | (no source changes) | TODO |

Each commit = one session. After each commit, update this PICKUP before ending the session.

---

## Next Session: Commit P2-A

**Goal:** Add `generic_flat_cache.h` and `generic_flat_cache_policies.h`, including all three policies (`CompactStatePolicy`, `HashOnlyPolicy`, `PredecessorPolicy`) and three mandatory cluster-size `static_assert`s. No wiring. No existing file modifications. Build must succeed with `-DUSE_GENERIC_CACHE=ON` even though nothing calls the template yet.

**Key constraints (from `phase2_plan.md`):**

- C++14 only. No `if constexpr`. Use tag dispatch or SFINAE for the `HAS_HASH_GUARD` branch in `PredecessorPolicy`.
- Static assertions required (MANDATORY, human-signed-off values):
  - `sizeof(generic_flat_cache<CompactStatePolicy>::cluster) == 64`
  - `sizeof(generic_flat_cache<HashOnlyPolicy>::cluster)     == 16`
  - `sizeof(generic_flat_cache<PredecessorPolicy>::cluster)  == 128`
  - plus matching `alignof` assertions on the first and third.
- Do NOT modify `flat_cache.*`, `hash_only_cache.*`, `predecessor_flat_cache.*`, `cache_factory.h`, or `game_state.*`. P2-A is additive only.
- Do NOT add the `#ifdef USE_GENERIC_CACHE` dispatch to `cache_factory.h` — that is P2-C.

**Reference implementations to read first:**

- `src/main/game/flat_cache.h` / `flat_cache.cpp` — the TwoBig1 depth-preferred replacement.
- `src/main/game/hash_only_cache.h` / `hash_only_cache.cpp` — simpler no-depth TwoBig1.
- `src/main/game/predecessor_flat_cache.h` / `predecessor_flat_cache.cpp` — the hash-guard optimisation on slot 1.

The template should reproduce each original's behaviour exactly when instantiated with the matching policy. Preserving the hash-guard optimisation in `PredecessorPolicy` is the subtlest requirement — it cannot be lost.

**Build command for P2-A:**

```bash
cd cmake-build-debug && cmake -DUSE_GENERIC_CACHE=ON .. && make -j4
# Expected: compiles clean; nothing calls the template yet.
```

**Validation checklist for P2-A:**

- [ ] New header files compile standalone under `-DUSE_GENERIC_CACHE=ON`.
- [ ] Three cluster-size `static_assert`s present and passing.
- [ ] Two `alignof` assertions present and passing.
- [ ] `flat_cache.*`, `hash_only_cache.*`, `predecessor_flat_cache.*`, `cache_factory.h` are byte-identical to dev.
- [ ] Default build (`USE_GENERIC_CACHE=OFF`) is unaffected — full unit test suite same as baseline.

---

## Build Commands for This Phase

```bash
# Default build (USE_GENERIC_CACHE=OFF) — must always pass identically to dev
./build.sh --debug --unit-tests
./build.sh --release --unit-tests

# With template routing enabled
cd cmake-build-release && cmake -DUSE_GENERIC_CACHE=ON .. && make -j4
ctest -R unit_tests --output-on-failure

# Regression levels for P2-E
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
cd cmake-build-release && ctest -R regression_level2 --output-on-failure
```

---

## Test Status (Phase 2 baseline — same as Phase 1 final)

- `ZobristIncremental.*`, `FaceUpCards.*`: ALL PASS.
- `SolverCacheSelectionTest.BlackHoleUsesNewCache`: FAIL — pre-existing (KI-3), debug only, times out with 10k cache and -O0. Ignore.
- `PredecessorDualCacheTest.AccordionAgreement`: FAIL / CRASH — pre-existing (KI-7), accordion out of scope. Ignore.
- `Klondike.*`, `Somerset.*`, etc.: pass via CTest from repo root; SKIP if run directly from `cmake-build-debug/`.

Phase 2 **must not introduce** any new test failures. If a new failure appears that is not KI-3 or KI-7, it is a bug — stop and report.

---

## Known Issues / Deferred Items

All inherited from Phase 1's `PICKUP.md`. Not repeated here in detail — read `PICKUP.md` §"Known Issues". Phase 2 does not address any of them.

- **KI-1** — `initially_face_up[52]` not valid for 2-deck games. Deferred; 2-deck uses LRU.
- **KI-2** — descriptor name confusion. Deferred.
- **KI-3** — BlackHoleUsesNewCache debug timeout. Pre-existing, ignore.
- **KI-4** — `init_payload_and_hash` ordering. Deferred.
- **KI-5** — (resolved in Phase 1, tests re-enabled).
- **KI-6** — (resolved in Phase 1, assert added).
- **KI-7** — AccordionAgreement crash. Pre-existing, accordion out of scope, IGNORE in Phase 2.

---

## Current Blocker

*(none — Phase 2 not started)*

---

## End-of-Session Protocol (reminder)

After each commit in this phase:
1. Tick off the validation checklist for the committed commit.
2. Update this PICKUP: mark commit as DONE, add its git hash, update "Next Session" to point at the next commit.
3. Show Ian the diff of this PICKUP so he can confirm status before the session ends.
