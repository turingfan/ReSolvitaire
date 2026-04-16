# Phase 3 Status: Conditional Compilation — Pickup Document

**Date written:** 2026-04-14
**Branch:** `feature/conditional-compilation` (to be created from `dev` after Phase 2 merges)
**Plan:** `docs/refactoring/phase3_plan.md`
**Workflow:** `docs/refactoring/phase2_3_workflow.md`
**Status:** P3-B DONE; next session is P3-C.

---

## PROCESS RULES — READ FIRST

1. **Bug encountered → STOP and report to Ian.** Do not investigate. Do not attempt a fix. Write the symptom in one paragraph under "Current Blocker" below and ask how to proceed.
2. **Semantic question → STOP and ask Ian.** He is the domain expert on descriptors, cache semantics, and correct behaviour.
3. **Scope → exactly one named commit per session.** Do not proceed to the next commit without Ian's explicit instruction.
4. **Test failure → a bug report, not a debugging task.**
5. **KI-7 accordion failures are EXPECTED. Do NOT investigate accordion.**

---

## What We Are Trying to Do

Produce three variant solver binaries from the same source tree by introducing compile-time flags that strip out code each variant does not use. The default `solvitaire` binary is **not changed**.

| Binary | Cache | Zobrist hash update compiled in? |
|---|---|---|
| `solvitaire-flat` | `generic_flat_cache<CompactStatePolicy>` only | Yes |
| `solvitaire-hash-only` | `generic_flat_cache<HashOnlyPolicy>` only | Yes |
| `solvitaire-lru` | `lru_cache` only | **No** |

### `--force-lru` semantics for variant binaries

- **`solvitaire-lru`**: runs normally for games that already route to LRU (2-deck, spider, accordion, suit-symmetry). For flat-cache-eligible games, **requires `--force-lru`** as an explicit benchmarking opt-in; fails without it.
- **`solvitaire-flat`**: rejects `--force-lru` unconditionally — error.
- **`solvitaire-hash-only`**: rejects `--force-lru` unconditionally — error.
- **Standard `solvitaire`**: unchanged.

The full plan with compile flags, factory dispatch, `compare_binaries.sh`, and success criteria is in `phase3_plan.md`. Read it before starting any commit.

---

## Prerequisites Before Session 1

**Claude runs these, each gated on Ian's explicit "go" for that step** (see `phase2_3_workflow.md` §"Concrete git operations"):

1. **Checkpoint 3 — Land Phase 2 to `dev`.** Claude states the merge commands, waits for Ian's "go," then runs:
   ```bash
   git checkout dev && git pull
   git merge --no-ff feature/template-cache -m "Phase 2: template cache unification"
   git push
   ```
2. **Checkpoint 4 — Create `feature/conditional-compilation` from `dev`.** Claude states the command, waits for Ian's "go," then runs:
   ```bash
   git checkout -b feature/conditional-compilation dev
   git push -u origin feature/conditional-compilation
   ```
3. **Baseline confirmation (no approval needed, local build only):**
   ```bash
   ./build.sh --release --unit-tests
   cd cmake-build-release && ctest -R unit_tests --output-on-failure
   # Expected: two pre-existing known failures only (KI-3, KI-7). Everything else passes.
   ```
   If baseline is not clean, stop and report — do NOT proceed to P3-A.

Once Checkpoints 3 and 4 are green and the baseline is confirmed, Session 1 starts on commit P3-A.

---

## Commits Planned

| ID | Title | Key files | Status |
|---|---|---|---|
| P3-A | Add `SOLVITAIRE_COMPUTES_FLAT_HASH` guard macro + wrap Zobrist update calls | `src/main/game/search-state/game_state.cpp` | DONE (`c355f85`) |
| P3-B | CMake variant targets + factory dispatch (incl. `--force-lru` validation) | `CMakeLists.txt`, `src/main/game/cache_factory.h` | DONE (`b0e418f`) |
| P3-C | Error handling for ineligible flag+game combinations in `main.cpp` | `src/main/main.cpp` | TODO |
| P3-D | `compare_binaries.sh` validation harness | `scripts/compare_binaries.sh` (new), `CMakeLists.txt` | TODO |
| P3-E | Regression Level 1 per variant binary | `CMakeLists.txt` | TODO |

Each commit = one session. After each commit, update this PICKUP before ending the session.

---

## Build Commands for This Phase

```bash
# Default build — must always pass identically to dev
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R unit_tests --output-on-failure

# After P3-B: build all four binaries
cd cmake-build-release && make -j4
# Binaries: bin/solvitaire  bin/solvitaire-flat  bin/solvitaire-hash-only  bin/solvitaire-lru

# compare_binaries.sh (after P3-D)
./scripts/compare_binaries.sh

# Verify Zobrist stripped from solvitaire-lru (after P3-B)
nm cmake-build-release/bin/solvitaire-lru | grep -i zobrist | grep -v ' U '
# Expected: only zobrist_hash::init present; update_zobrist_for_* absent

# Regression variants (after P3-E)
cd cmake-build-release
ctest -R regression_level1_flat --output-on-failure
ctest -R regression_level1_lru --output-on-failure
ctest -R regression_level1_hash_only --output-on-failure
```

---

## Test Status (Phase 3 baseline — same as Phase 2 final)

- `ZobristIncremental.*`, `FaceUpCards.*`: ALL PASS.
- `SolverCacheSelectionTest.BlackHoleUsesNewCache`: FAIL — pre-existing (KI-3), debug only, times out with 10k cache and -O0. Ignore.
- `PredecessorDualCacheTest.AccordionAgreement`: FAIL / CRASH — pre-existing (KI-7), accordion out of scope. Ignore.
- `Klondike.*`, `Somerset.*`, etc.: pass via CTest from repo root; SKIP if run directly from `cmake-build-debug/`.

Phase 3 **must not introduce** any new test failures beyond KI-3 and KI-7.

---

## Known Issues / Deferred Items

Inherited from earlier phases; Phase 3 does not address any of them.

- **KI-1** — `initially_face_up[52]` not valid for 2-deck games. Deferred; 2-deck uses LRU.
- **KI-2** — descriptor name confusion. Deferred.
- **KI-3** — BlackHoleUsesNewCache debug timeout. Pre-existing, ignore.
- **KI-4** — `init_payload_and_hash` ordering. Deferred.
- **KI-7** — AccordionAgreement crash. Pre-existing, accordion out of scope, IGNORE.

---

## Current Blocker

*(none — Phase 3 not yet started; awaiting Phase 2 merge to `dev`)*

---

## End-of-Session Protocol (reminder)

After each commit in this phase:
1. Tick off the validation checklist for the committed commit.
2. Update this PICKUP: mark commit as DONE, add its git hash, update status.
3. Show Ian the diff of this PICKUP so he can confirm status before the session ends.
