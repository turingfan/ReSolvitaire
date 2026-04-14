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
| P2-A | Add `generic_flat_cache.h` + 3 policies + static_asserts | `src/main/game/generic_flat_cache.h` (new), `generic_flat_cache_policies.h` (new) | DONE `525e855` |
| P2-B | Unit tests for each specialisation | `src/test/unit_tests/generic_flat_cache_test.cpp` (new) | DONE `53ead67` |
| P2-C | Wire `cache_factory.h` behind `USE_GENERIC_CACHE` | `cache_factory.h`, `CMakeLists.txt` | DONE `TBD` |
| P2-D | DualCache parity harness (non-accordion only) | `src/test/unit_tests/generic_flat_dual_cache_test.cpp` (new) | TODO |
| P2-E | Regression L1 + L2 under `USE_GENERIC_CACHE=ON` | (no source changes) | TODO |

Each commit = one session. After each commit, update this PICKUP before ending the session.

---

## Next Session: Commit P2-D

**Goal:** DualCache parity harness for `CompactStatePolicy` and `HashOnlyPolicy`
(non-accordion only). New file `src/test/unit_tests/generic_flat_dual_cache_test.cpp`.
For each of the two policies:
- Pair `generic_flat_cache<Policy>` (primary) against the original (`flat_cache` or
  `hash_only_cache`) in a `dual_cache`.
- Run the solver on 5 klondike seeds and 5 free-cell seeds with cap = 100 000.
- `get_lru_only_hits()` and `get_flat_only_hits()` must both be zero.

`PredecessorPolicy` parity is deliberately deferred (KI-7): add a
`DISABLED_PredecessorParity` test with an explanatory comment.

Key files: `src/test/unit_tests/generic_flat_dual_cache_test.cpp` (new), `CMakeLists.txt`.

**Key constraint:** Do NOT use `USE_GENERIC_CACHE` flag in the test — construct
`generic_flat_cache<Policy>` and the original explicitly, side by side.

---

## Completed Commits

### P2-C — `TBD` ✅

**Validation checklist:**
- [x] Default build (OFF): clean, no warnings.
- [x] Default build: `unit_tests` and `unit_tests_full` 100% pass (identical to P2-B).
- [x] Generic build (ON): `unit_tests` and `unit_tests_full` 100% pass (same pattern).
- [x] Reset to OFF before commit.
- [x] `flat_cache.*`, `hash_only_cache.*`, `predecessor_flat_cache.*` byte-identical to P2-B (untouched).

**Design notes:**
- `cache_factory.h`: added `#ifdef USE_GENERIC_CACHE` conditional include for
  `generic_flat_cache.h` (which already includes the policies header). Three
  specialised branches each wrapped with `#ifdef`/`#else`/`#endif`; `lru_cache`
  fallback unchanged.
- `CMakeLists.txt`: `option(USE_GENERIC_CACHE ... OFF)` + `add_compile_definitions`
  guard inserted after the existing `BOOST_LOG_DYN_LINK` definition.

---

### P2-B — `53ead67` ✅

**Validation checklist:**
- [x] Build clean, no warnings.
- [x] Three cluster-size `EXPECT_EQ` runtime checks present and passing.
- [x] 19 new tests across three suites (CompactState ×7, HashOnly ×6, Predecessor ×6).
- [x] `flat_cache.*`, `hash_only_cache.*`, `predecessor_flat_cache.*`, `cache_factory.h` byte-identical to P2-A (untouched).
- [x] Default build unaffected — `unit_tests` and `unit_tests_full` 100% pass (same as P2-A baseline).

**Design notes:**
- Each suite mirrors its corresponding original test file for easy parity comparison.
- `EvictionWorks` uses capacity=2 (1 cluster, 2 slots) with 200-move walk; cycling via `moves[i % moves.size()]`.
- `PredecessorPolicy` suite uses accordion rules; no KI-7 failures observed in unit test surface.

---

### P2-A — `525e855` ✅

**Validation checklist:**
- [x] New header files compile standalone (build clean, no warnings).
- [x] Three cluster-size `static_assert`s present and passing.
- [x] Two `alignof` assertions present and passing.
- [x] `flat_cache.*`, `hash_only_cache.*`, `predecessor_flat_cache.*`, `cache_factory.h` byte-identical to dev (untouched).
- [x] Default build unaffected — `unit_tests` 100% pass (same as baseline).

**Design notes (for future sessions):**
- Tag dispatch uses two axes: `hash_guard_tag`/`no_hash_guard_tag` for the slot-1
  contains/insert check; `insert_simple/depth/predecessor_tag` for replacement policy.
- `generic_flat_cache.h` includes `generic_flat_cache_policies.h`; the compile-test
  `.cpp` includes only `generic_flat_cache.h`.
- `CMakeLists.txt` one-line change: added compile-test to `sources_test_unit`.

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
