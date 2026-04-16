# Phase 3 Status: Conditional Compilation — Pickup Document

**Date written:** 2026-04-14
**Branch:** `feature/conditional-compilation` (to be created from `dev` after Phase 2 merges)
**Plan:** `docs/refactoring/phase3_plan.md`
**Workflow:** `docs/refactoring/phase2_3_workflow.md`
**Status:** P3-D DONE; next session is P3-B-fix-1 then P3-B-fix-2 (see §Bugs Found and §Commits Planned).

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

## Bugs Found in Agent Work (P3-B review)

Two bugs were identified when P3-B through P3-D were evaluated:

### Bug 1 — Predecessor routing inverted (P3-B)
`solvitaire-flat` was refusing accordion games; `solvitaire-lru` was silently running them via `lru_cache`. The predecessor flat cache is a flat-array cache and belongs in `solvitaire-flat`, not `solvitaire-lru`. Additionally `predecessor_flat_cache.h/cpp` was compiled unconditionally into all variant binaries, polluting `solvitaire-lru`'s symbol table with Zobrist references.

**Fix is local but not yet committed** — three files changed: `cache_factory.h`, `predecessor_flat_cache.cpp`, `solver.cpp`. See §P3-B-fix-1 below.

### Bug 2 — P3-A guards were too narrow
`SOLVITAIRE_COMPUTES_FLAT_HASH` only wrapped the Zobrist XOR lines. The `payload.set_*()` calls, the `compact_state payload` and `zobrist_hash_value` members, and the flat-cache `.cpp` files all still compile into `solvitaire-lru`. Additionally the **default binary** wastes cycles computing hash and payload for LRU-routed games at runtime (spider, 2-deck, suit-symmetry, accordion).

The fix requires two commits. Full detail in `docs/refactoring/WIP-P3-B-fixup-plan.md`.

Long-term architectural solution (templated game_state with single runtime dispatch) documented in `docs/proposals/PROPOSAL-templated-game-state-dispatch.md` and `docs/known-issues.md` §8. Deferred until after this workpackage.

---

## Commits Planned

| ID | Title | Key files | Status |
|---|---|---|---|
| P3-A | Add `SOLVITAIRE_COMPUTES_FLAT_HASH` guard macro + wrap Zobrist update calls | `game_state.cpp` | DONE (`c355f85`) |
| P3-B | CMake variant targets + factory dispatch (incl. `--force-lru` validation) | `CMakeLists.txt`, `cache_factory.h` | DONE (`b0e418f`) — **has bugs, see fixes below** |
| P3-C | Error handling for ineligible flag+game combinations in `main.cpp` | `main.cpp` | DONE (`a62f126`) |
| P3-D | `compare_binaries.sh` validation harness | `scripts/compare_binaries.sh`, `CMakeLists.txt` | DONE (`5d0ccb8`, timeout fix `a025bd1`) |
| **P3-B-fix-1** | **Fix predecessor routing + strip from LRU binary** | `cache_factory.h`, `predecessor_flat_cache.cpp`, `solver.cpp` | **LOCAL ONLY — commit this first** |
| **P3-B-fix-2 (Commit 1)** | **Runtime boolean guards in default binary** | `cache_interface.h`, `game_state.h`, `game_state.cpp`, `solver.cpp` | TODO |
| **P3-B-fix-2 (Commit 2)** | **Compile-time guards for variant binaries** | `game_state.h`, `game_state.cpp`, flat-cache `.cpp` files, `solver.cpp`, `cache_factory.h` | TODO |
| P3-E | Regression Level 1 per variant binary | `CMakeLists.txt`, `regression_runner.py` | TODO — see §P3-E Design below |

---

## P3-B-fix-1 — What Is Already Changed Locally

**`src/main/game/cache_factory.h`**:
- `SOLVITAIRE_LRU_ONLY`: added `use_predecessor_cache` rejection before lru_cache return
- `SOLVITAIRE_FLAT_ONLY`: replaced predecessor rejection with `return std::make_unique<predecessor_flat_cache>(capacity)`
- `#include "predecessor_flat_cache.h"` guarded with `#if !defined(SOLVITAIRE_LRU_ONLY)`

**`src/main/game/predecessor_flat_cache.cpp`**: entire content wrapped with `#if !defined(SOLVITAIRE_LRU_ONLY)`

**`src/main/solver/solver.cpp`**: `#include` and `dynamic_cast<predecessor_flat_cache*>` guarded with `#if !defined(SOLVITAIRE_LRU_ONLY)`

Smoke tests verified locally. **Commit this before starting P3-B-fix-2.**

---

## P3-B-fix-2 — Two-Commit Plan

Full detail in `docs/refactoring/WIP-P3-B-fixup-plan.md`. Summary:

### Commit 1 — Runtime boolean guards (default binary)

Add to `cache_interface.h`:
```cpp
inline bool needs_flat_hash(const sol_rules& rules, bool suit_sym,
                             bool force_lru, const std::string& cache_type) {
    if (force_lru) return false;
    return use_predecessor_cache(rules) || cache_type == "hash-only"
        || use_new_cache(rules, suit_sym);
}
inline bool needs_flat_payload(const sol_rules& rules, bool suit_sym,
                                bool force_lru, const std::string& cache_type) {
    if (force_lru) return false;
    if (cache_type == "hash-only") return false;
    return use_predecessor_cache(rules) || use_new_cache(rules, suit_sym);
}
```

Add `bool computing_flat_hash` and `bool computing_flat_payload` to `game_state` (set at construction via the helpers above — the routing info is available before game_state is constructed).

Guard every hash/payload operation in `game_state.cpp` with the appropriate bool (see WIP plan for corrected per-site details — in particular, `init_payload_and_hash()` and all `payload.set_*` calls inside update helpers are guarded by `computing_flat_hash`, not `computing_flat_payload`, because descriptors are tracked only in `payload` and must be kept current whenever the hash is maintained). Guard `set_payload_depth()` and `assert_payload_consistent()` call sites in `solver.cpp` with `computing_flat_payload`.

**Test gate:** `ctest -R unit_tests` and `ctest -R regression_level1` on default binary. No new failures. Variant binaries still build.

### Commit 2 — Compile-time guards (variant binaries)

Guard `compact_state payload`, `zobrist_hash_value`, and related method declarations in `game_state.h` with `#if SOLVITAIRE_COMPUTES_FLAT_HASH`.

Wrap entire content of `flat_cache.cpp`, `hash_only_cache.cpp`, and (after checking) `compact_state.cpp` and `parent_table.cpp` with `#if !defined(SOLVITAIRE_LRU_ONLY)`.

Guard flat-cache `#include`s and `dynamic_cast`s in `solver.cpp` and `cache_factory.h`.

**Test gate:** All four variant binaries build clean. `nm` on `solvitaire-lru` shows no flat-cache symbols. Default binary regression still passes.

**Known unknowns to check before Commit 2:** Does `compact_state.cpp` or `parent_table.cpp` contain anything used outside the flat-cache path? Does `dual_cache` need guarding in `solver.cpp`?

---

## P3-E Design Notes

P3-E (regression_level1 per variant) has a design constraint that was not in the original plan:

- Level 1 oracle has **46 instances with `streamliner: "both"`** (suit-symmetry active) — these route to LRU; `solvitaire-flat` and `solvitaire-hash-only` reject them
- Level 1 has **10 `gaps-basic-variant` instances** (sequence_count > 0, LRU-only) — rejected by flat and hash-only
- Level 1 has **10 `late-binding-solitaire` instances** (accordion) — now accepted by `solvitaire-flat` after fix; rejected by `solvitaire-lru`

`regression_runner.py` needs two new arguments:
- `--allowed-streamliners LIST` — skip oracle entries whose streamliner is not in the list
- `--force-lru` — append `--force-lru` to every solver invocation
- `--skip-ineligible` — treat non-zero exit with "requires" / "not eligible" in stderr as a skip

CTest targets:
- `regression_level1_flat`: `--allowed-streamliners none` (factory rejects LRU-only games as skip)
- `regression_level1_hash_only`: `--allowed-streamliners none` (same subset, documented allow-list)
- `regression_level1_lru`: `--force-lru` all 150 instances (suit-sym games pass naturally; accordion rejected → skip)

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
