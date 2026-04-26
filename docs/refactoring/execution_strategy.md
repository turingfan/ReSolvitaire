# Execution Strategy: Architectural Refactoring

*Generated 2026-04-09. Covers the approved plan: Zobrist inline computation, cache template unification, conditional compilation, legacy solver tagging.*

---

## Preliminary Assessment

Three things immediately shape the strategy after reading the code:

1. **The dual-cache framework already exists.** `dual_cache.h` wraps a primary and reference cache and asserts agreement pre-eviction. This is the exact tool for cache unification validation. It shouldn't be rebuilt.

2. **Zobrist inline is feasible but non-trivial.** The undo stack captures `old_desc`, `old_*_found_rank`, `old_hole_top`, `old_waste_ptr` — values that CAN be reconstructed from pile state at undo time, but the ordering of pile-ops vs. hash-ops must be inverted from the current pattern. `revealed_card_id` is the subtlest case (readable from `piles[m.from][0]` before pile-undo, not stored in `move`). Each of the 6 move types needs individual analysis.

3. **The three fast caches are structurally identical.** Same Fibonacci hashing, same TwoBig1 replacement, same cluster layout — they differ only in payload type and cluster size. Template unification is the lowest-risk change in the plan.

---

## Phase Decomposition

### Phase 0: Harden the Safety Net *(prerequisite, no functional change)*

**Goal:** Establish validators that will catch bugs introduced in later phases. Nothing ships without these passing.

**What to do:**

1. Extend `zobrist_test.cpp` to add a **round-trip sequence test**: run 50-move random sequences, verify `hash_after_make_then_undo == hash_before`. This doesn't test the *method* of undo, just the result. It will serve as the oracle for Phase 1.

    1. Add a **hash snapshot comparison test**: in `dual_cache_test.cpp` or a new `hash_consistency_test.cpp`, run a solver search on a small fixed seed, recording `zobrist_hash_value` at every node. This golden trace is replayed in Phase 1 to verify inline undo produces bit-identical hashes.

2. Ensure `unit_tests_full` (including `DualCacheTest`) passes cleanly on the current `dev` branch. This is the baseline.

**Branch:** `dev` (no feature branch — these are pure test additions)
**Merge criterion:** All existing tests pass + new tests pass
**Protected files (no agentic modification):** `dual_cache.h`, `zobrist_test.cpp` (add to, don't modify structure)

**Risk:** Low. Test-only changes.

---

### Phase 1: Zobrist Inline Computation

**Goal:** Remove `zobrist_undo_stack` and `pred_undo_entries`; compute hash deltas inline in `undo_move`.

**Critical prerequisite (human judgment required before any code is written):**

For each move type, verify the inline reconstruction logic is sound. The pattern is:
- For `undo_regular_move`: `old_desc` is recoverable via `determine_destination_descriptor(m.from, card)` called **before** pile-undo (at that point, m.from holds what was below the card, exactly as it was when the card was placed there by a prior make_move)
- `revealed_card_id` is readable from `piles[m.from][0]` before pile-undo
- `old_hole_top` is readable from `piles[hole]` **after** pile-undo (card removed, old top visible)
- `old_waste_ptr` is recoverable via `effective_waste_ptr()` after pile-undo
- Foundation ranks are recoverable from pile state at both points

This analysis needs to be documented and reviewed for all 6 move types (regular, built_group, stock_k_plus, stock_to_all_tableau, sequence, accordion) before any implementation starts.

**Implementation approach:**

Do NOT delete the undo stack immediately. First add `assert` cross-checks:
```cpp
void game_state::undo_regular_move(const move m) {
    zobrist_undo undo = zobrist_undo_stack.back(); // keep for now

    // Inline reconstruction (new)
    uint8_t inline_old_desc = compute_old_desc_inline(m); // new function
    assert(inline_old_desc == undo.old_desc);              // validator

    // ... rest unchanged
    zobrist_undo_stack.pop_back();
}
```

This dual-tracking mode runs both paths simultaneously, catching any inline reconstruction errors before the stack is removed. Run all 5 regression levels with dual-tracking enabled.

**Only after all regression levels pass** with the `assert` checks, remove the stack.

**Validation checklist:**
- [ ] Phase 0 golden trace still matches bit-for-bit
- [ ] All unit tests pass (`ctest -R unit_tests`)
- [ ] Level 1 regression passes
- [ ] Level 2 regression passes (catches edge cases like Canfield waste pointer)
- [ ] Peak memory reduced (verify with macOS `leaks` or `/usr/bin/time -v`)

**Branch:** `feature/zobrist-inline` from `dev`
**Merge criterion:** Level 2 passes, golden hash trace matches, clean Level 1
**Most likely failure:** `old_hole_top` reconstruction failing for the hole game type; `sat_count` edge case in stock_to_all_tableau where fewer than `count` cards remain in stock
**Rollback:** Trivially: delete branch, no impact on `dev`
**Protected from agentic modification:** The `assert` cross-check expressions themselves (human writes and signs off on the reconstruction formulas)

---

### Phase 2: Template Cache Unification

**Goal:** Create `generic_flat_cache<PayloadPolicy>` that subsumes `flat_cache`, `hash_only_cache`, and `predecessor_flat_cache`. **The three originals remain unchanged until Phase 5.**

**Implementation approach:**

Add the new template alongside — do not modify or delete existing caches:
```
src/main/game/generic_flat_cache.h   ← new
src/main/game/flat_cache.h/cpp       ← unchanged
src/main/game/hash_only_cache.h/cpp  ← unchanged
src/main/game/predecessor_flat_cache.h/cpp ← unchanged
```

Wire the new template into `cache_factory.h` behind a compile flag:
```cpp
#ifdef USE_GENERIC_CACHE
    // Use generic_flat_cache<...>
#else
    // Use original caches
#endif
```

This lets CI run both paths and compare.

**Validation:**

Use the existing `DualCacheTest` / `dual_cache.h` framework. Before shipping, add a variant that pairs `generic_flat_cache<CompactStatePolicy>` as primary against the original `flat_cache` as reference. If they agree pre-eviction on 5000+ ops, the template is behaviorally equivalent.

**Memory layout check (critical — must be human-reviewed):**

Add `static_assert`s for all three specializations before code review:
```cpp
static_assert(sizeof(generic_flat_cache<CompactStatePolicy>::cluster) == 64);
static_assert(sizeof(generic_flat_cache<PredecessorPolicy>::cluster) == 128);
static_assert(sizeof(generic_flat_cache<EmptyPolicy>::cluster) == 16);
```
Template specialization can silently add padding. These asserts are mandatory.

**Validation checklist:**
- [ ] `static_assert`s on cluster sizes for all three specializations
- [ ] `DualCacheTest` with generic vs. original for each specialization (5000+ ops)
- [ ] Level 1 regression with `USE_GENERIC_CACHE` flag
- [ ] Hash guard optimization preserved in predecessor specialization (check with perf counter or benchmark)

**Branch:** `feature/template-cache` from `dev` (NOT from Phase 1 branch — phases are independent)
**Merge criterion:** DualCache agreement, cluster size static_asserts pass, Level 1 clean
**Most likely failure:** Template instantiation changing cluster alignment/size; hash guard optimization not being preserved in predecessor specialization
**Rollback:** Delete branch, original caches unaffected

---

### Phase 3: Conditional Compilation

**Sequencing:** This phase should be based on the merged result of Phases 1+2. It is the highest-risk phase for silent failures.

**Goal:** Build system emits `solvitaire-flat`, `solvitaire-lru`, `solvitaire-hash-only` from compile-time flags. Zobrist computation is conditionally compiled out of the LRU path.

**Do NOT do this first:**
- Do not remove any code paths
- Do not default any build to a single mode
- Do not change what the default `solvitaire` binary does

**Approach:**

1. Add three CMake targets as aliases that set compile flags; the default `solvitaire` target remains unchanged with all code paths active.

2. Wrap `#ifdef USE_FLAT_CACHE` only around the Zobrist *update* calls inside `make_move`/`undo_move`. The data structures stay unconditional initially.

3. After CI validates all three binaries agree on a test instance, begin conditionally compiling the data structures themselves.

**Validation harness (new script required):**

```bash
# scripts/compare_binaries.sh
for seed in 1 2 3 4 5; do
    flat_result=$(./solvitaire-flat --type klondike --random $seed --json)
    lru_result=$(./solvitaire-lru  --type klondike --random $seed --json)
    hash_result=$(./solvitaire-hash-only --type klondike --random $seed --json)
    assert_fields_equal $flat_result $lru_result outcome
    assert_fields_equal $flat_result $hash_result outcome
done
```

Outcomes must match. `states_searched` will differ for hash-only (false positives are expected and intentional) — document this explicitly.

**Checklist:**
- [ ] Default `solvitaire` binary behavior unchanged (regression Level 1)
- [ ] All three variants produce correct outcomes on 50-seed klondike test set
- [ ] `solvitaire-lru` compiled without `USE_FLAT_CACHE` does NOT compute Zobrist (verified with `nm` or code review of emitted binary)
- [ ] Build system emits a clear error when trying to run an ineligible game type on an incompatible binary

**Branch:** `feature/conditional-compilation` from `dev` (after 1+2 merged)
**Most likely failure:** A `#ifdef` guarding the wrong scope; missing flag on a translation unit; accidentally breaking the default build
**Rollback:** Revert CMakeLists.txt changes; all functional code unchanged

---

### Phase 4: Legacy Solver Tagging

**Goal:** Establish a frozen, benchmarkable legacy baseline. This is NOT a code change — it is a git operation.

**Approach:**

1. Tag the current `dev` HEAD (before any Phase 1–3 changes merge) as `v-pre-refactor` immediately.

2. After refactoring is complete (all phases merged), tag the new HEAD as `v-post-refactor`.

3. `run_benchmark.py --legacy` invokes the `v-pre-refactor` binary built from that tag.

The legacy solver should be a **frozen tag**, not a long-lived branch. Branches imply ongoing maintenance; a tag is a snapshot. Build it once, archive the binary if needed.

**No code changes required.** The `--legacy` flag planned for `run_benchmark.py` completes this phase (see `task_legacy_solver_support.md`).

---        

### Phase 5: Cleanup *(after all phases validated)*

Only after Levels 1–3 regression pass on all variants:
- Delete `flat_cache.cpp`, `hash_only_cache.cpp`, `predecessor_flat_cache.cpp`
- Remove `#ifdef USE_GENERIC_CACHE` guards
- Remove undo stack code paths and dual-tracking asserts

This phase is mechanical and low-risk because every deletion was already validated as equivalent.

---

## Git Workflow

```
dev (integration, always green)
 │
 ├── feature/phase0-safety-net      ← test additions only
 │   └── PR: new hash tests + Level1 baseline
 │
 ├── feature/zobrist-inline         ← from dev (after Phase 0 merged)
 │   └── requires: assert cross-checks pass L1+L2
 │   └── PR: human reviews inline reconstruction formulas
 │
 ├── feature/template-cache         ← from dev (parallel with Phase 1)
 │   └── requires: DualCache agreement + static_asserts
 │   └── PR: human reviews template specialization sizes
 │
 ├── feature/conditional-compilation ← from dev (after 1+2 merged)
 │   └── requires: compare_binaries.sh passes 50 seeds
 │   └── PR: human reviews CMakeLists changes
 │
 └── feature/cleanup                ← from dev (after 3 merged + L3 pass)
     └── requires: L1+L2+L3 all pass
     └── PR: only deletions, no logic changes
```

**Merge criteria (non-negotiable):**
- No PR merges to `dev` unless `ctest -R unit_tests` and `ctest -R regression_level1` pass in CI
- Phases 1 and 2 can run in parallel (independent branches from `dev`)
- Phase 3 only starts after both 1 and 2 are in `dev`
- Phase 5 only starts after Level 3 regression passes

---

## Agentic Safeguards

### Files requiring human sign-off before any modification

| File | Why |
|---|---|
| `game_state.h` / `game_state.cpp` | Core correctness — inline undo reconstruction logic |
| `CMakeLists.txt` | Conditional compilation flags — silent wrong builds |
| `dual_cache.h` | The validation oracle itself — must not be changed to pass tests |
| `cache_factory.h` | Cache routing — wrong routing is a correctness bug, not just perf |
| `zobrist.h` | Hashing tables — any change corrupts all cache entries |

### Decisions requiring human judgment (not agentic)

1. **"Is inline undo reconstruction correct for move type X?"** — requires mathematical verification that the before/after pile state yields identical hash delta. Agentic code can implement it; human must verify the formula.

2. **"Does the template specialization preserve memory layout?"** — needs `static_assert` values verified by a human, not just accepted because they compile.

3. **"Are the conditional compilation boundaries correct?"** — easy to accidentally leave Zobrist on in the LRU path or forget to guard a translation unit.

4. **Any changes to the Phase 0 test oracles.** If tests need to change to pass, that's a bug in the code, not the test.

### Scope constraints per phase

- **Phase 0**: Agents may add test cases but may not modify existing test structure or assertions
- **Phase 1**: Agents may add the `assert` cross-checks and the inline functions; human approves before stack removal
- **Phase 2**: Agents may write `generic_flat_cache.h`; human approves `static_assert` cluster sizes and DualCache pairing
- **Phase 3**: Agents may modify `CMakeLists.txt` to add new targets; human approves `#ifdef` scope in `game_state.cpp`

---

## Risk Register

| Phase | Failure Mode | Detection | Rollback |
|---|---|---|---|
| 1 | `old_hole_top` reconstructed incorrectly for hole game type | Level 2 regression; dual-tracking asserts | Delete branch |
| 1 | `sat_count` wrong when fewer cards remain in stock | `ctest -R golf` or stock game seeds | Delete branch |
| 2 | Template instantiation changes cluster alignment | `static_assert(sizeof(cluster) == N)` | Delete branch |
| 2 | Hash guard optimization lost in predecessor template | Benchmark hash miss rate vs. pre-refactor | Delete branch |
| 3 | `#ifdef` on wrong scope: LRU binary still computes Zobrist | `nm` or binary disassembly; benchmark shows no perf gain | Revert CMakeLists |
| 3 | `compare_binaries.sh` outcome mismatch | The script itself | Delete branch |
| 3 | Default `solvitaire` binary behavior changed | Level 1 regression | Revert CMakeLists |
| 5 | Deletion removes something still in use | Won't compile | `git revert` |

---

## What NOT to do

1. **Don't do Phases 1 and 3 simultaneously.** Inline Zobrist + conditional compilation in one branch means a failing test could be either change. Isolate them.

2. **Don't delete the original caches while Phase 2 is being validated.** The originals are the reference implementation — keep them until the template is proven equivalent.

3. **Don't run Level 4/5 regression to validate Phase 1.** Level 2 catches the edge cases (Canfield, hole games, stock dealing) and takes minutes, not hours. Save the expensive levels for pre-release validation.

4. **Don't use the `force_lru` path to skip validation.** If a new binary silently uses the wrong cache type, all results are invalid. The `cache_factory` routing must be explicitly tested.
