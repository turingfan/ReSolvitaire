# Phase 1: Incremental Inline Undo Implementation (DESIGN PHASE)

**Date:** 2026-04-10
**Status:** STRATEGY DEFINED, AWAITING IMPLEMENTATION ORDERING DECISIONS
**Scope:** Zobrist hash + compact_state payload inline undo. Excludes predecessor/accordion.
**Branch:** `feature/refactoring-phases`
**Prerequisite:** Phase 0 scaffolding complete (see `phase0_plan.md`)
**Approach:** Parallel computation with fallback copies; incremental per-component implementation
**Related:** `descriptor_undo_analysis.md` (in this directory)

---

## Overview

Phase 1 incrementally replaces each component of `zobrist_undo_stack` with inline computation, one component at a time. Each step is validated exhaustively before proceeding to the next.

At the end of Phase 1, the 10-byte `zobrist_undo` struct and its stack are **eliminated entirely** — no stored undo state is needed. See `descriptor_undo_analysis.md` for the proof that all values, including STARTING_FACE_UP, are recoverable from pile state using the face-down card invariant.

---

## Implementation Strategy

### High-Level Approach

Each move in the game happens **once**, with two independent hash/payload updates running in **parallel**:

1. **Existing path (primary):** The current undo code modifies `zobrist_hash_value` and `payload` as before
2. **New inline path:** Protected by `#ifdef VALIDATE_INLINE_UNDO`, a separate `inline_hash` and `inline_payload` are computed alongside
3. **For unimplemented move types:** The new path simply copies the reference values (fallback), allowing incremental implementation
4. **End-of-move assertion:** Assert that `inline_hash == zobrist_hash_value` and `inline_payload.matches(payload)`
5. **Incremental building:** Implement one move type at a time; once a move type's inline code is complete, remove the fallback copy

The pattern for each move type step is:

1. Write the inline computation code inside the `#ifdef VALIDATE_INLINE_UNDO` block for one move type (e.g., `undo_regular_move`)
2. Initially, for unimplemented components within that move type, copy the reference values
3. Implement one component of that move type (e.g., "pile operations")
4. Assert that the inline computation still matches the reference
5. Run unit tests + Level 1 regression with the validation build
6. When the assert passes on all tests, that component is proven correct
7. Commit and move to the next component
8. Repeat until all components for that move type are implemented, then move to the next move type

### The Parallel Computation Approach

The key insight is that we run both paths during the **same forward motion** through the undo operation:

**For each undo function:**
1. After the existing undo code completes (hash and payload are now updated):
   - Save the reference: `uint64_t expected_hash = zobrist_hash_value;` and `compact_state expected_payload = payload;`
2. Compute the inline path (protected by `#ifdef VALIDATE_INLINE_UNDO`):
   ```cpp
   uint64_t inline_hash = ...; // compute from scratch
   compact_state inline_payload = ...; // compute from scratch
   // For unimplemented components: copy the reference
   if (!inline_path_fully_implemented_for_this_move_type) {
       inline_hash = expected_hash;
       inline_payload = expected_payload;
   }
   ```
3. Assert the two paths agree:
   ```cpp
   assert(inline_hash == expected_hash && "INLINE UNDO: hash mismatch");
   assert(inline_payload.matches(expected_payload) && "INLINE UNDO: payload mismatch");
   ```
4. State remains as the existing code left it (correct for solver to continue)

This approach is clean because:
- No replay/undo/restore cycles — each move happens once
- Both paths update independently during the single forward pass
- Incremental implementation via fallback copies for unimplemented components
- Assertions catch divergence as components are added

---

## Implementation Strategy (Finalized)

### Approach

For each move type, **all components are implemented together** in a single pass. Both the forward path (`make_*_move`) and backward path (`undo_*_move`) are handled as a unit.

**Component scope (for each move type):**
1. Pile operations — card movement (in/out of piles)
2. Reveal handling — descriptor update (STARTING_FACE_UP → STARTING) if card turns face-down
3. Foundation ranks — hash update
4. Hole top — hash update (if applicable to game)
5. Waste pointer — hash update (if applicable to game)
6. Moved card descriptor — payload update

### Move Type Implementation Order (Sequential)

1. **Phase 1.1:** `make_regular_move` + `undo_regular_move` — Regular tableau/cell/reserve moves
2. **Phase 1.2:** `make_built_group_move` + `undo_built_group_move` — Composite group moves
3. **Phase 1.3:** `make_stock_k_plus_move` + `undo_stock_k_plus_move` — Stock draw/recycle with K+ flexibility
4. **Phase 1.4:** `make_stock_to_all_tableau_move` + `undo_stock_to_all_tableau_move` — Auto-play stock-to-tableau

### Reveal Move Handling

The reveal_move mechanism (turning a card face-down) is handled as part of the descriptor component within each move type. When a move has `reveal_move=true`, the inline path computes the descriptor change (STARTING_FACE_UP → STARTING) alongside other descriptor updates.

---

## Implementation Steps

### Step 1.1: Inline `make_regular_move` + `undo_regular_move`

**Files to modify:**
- `src/main/game/search-state/game_state.cpp` — `make_regular_move()` and `undo_regular_move()`

**Process:**
1. In `make_regular_move()`, add inline Zobrist hash and payload computation inside `#ifdef VALIDATE_INLINE_UNDO`
   - Compute hash change for card movement (pile removal + pile insertion)
   - Compute payload descriptor change for moved card (from STARTING/IN_CELL/etc. to IN_CELL/PARENT/etc.)
   - For components not yet applicable to regular moves, use fallback copy
2. In `undo_regular_move()`, add corresponding reverse computation inside `#ifdef VALIDATE_INLINE_UNDO`
   - Reverse the hash changes from make_regular_move
   - Reverse the payload descriptor changes
3. Add assertions at the end of both functions:
   - `assert(inline_hash == zobrist_hash_value && "...")`
   - `assert(inline_payload.matches(payload) && "...")`
4. Compile with `-DVALIDATE_INLINE_UNDO=ON`
5. Run `ctest -R unit_tests --output-on-failure`
6. Run `ctest -R regression_level1 --output-on-failure`
7. Commit: `feat(inline_undo): implement regular_move inline hash and payload computation`

### Step 1.2: Inline `make_built_group_move` + `undo_built_group_move`

Same pattern as Step 1.1, but for group move (multiple cards moved together).

### Step 1.3: Inline `make_stock_k_plus_move` + `undo_stock_k_plus_move`

Same pattern as Step 1.1, but includes:
- Waste pointer hash/payload updates
- Stock cycling logic

### Step 1.4: Inline `make_stock_to_all_tableau_move` + `undo_stock_to_all_tableau_move`

Same pattern as Step 1.1, but includes:
- Multiple destinations (auto-play to foundations and/or tableau)
- Waste pointer updates

### Final Step: Clean-up and remove undo stack

Once all four move types pass validation:
1. Remove `#ifdef VALIDATE_INLINE_UNDO` guards (keep inline code, remove assertions and reference copies)
2. Identify and remove obsolete `zobrist_undo_stack` and undo recording code
3. Clean up `zobrist_undo` struct and related helper functions (if unused elsewhere)
4. Run full regression suite (Levels 1–5) to ensure no regressions
5. Commit: `refactor: eliminate zobrist_undo_stack, inline all undo computation`

---

## Exit Criteria

- [ ] Step 1.1 complete: `make_regular_move` + `undo_regular_move` inline, all unit tests pass
- [ ] Step 1.2 complete: `make_built_group_move` + `undo_built_group_move` inline, all unit tests pass
- [ ] Step 1.3 complete: `make_stock_k_plus_move` + `undo_stock_k_plus_move` inline, all unit tests pass
- [ ] Step 1.4 complete: `make_stock_to_all_tableau_move` + `undo_stock_to_all_tableau_move` inline, all unit tests pass
- [ ] Level 1 regression passes with `VALIDATE_INLINE_UNDO=ON`
- [ ] Level 2 regression passes with `VALIDATE_INLINE_UNDO=ON` (comprehensive validation)
- [ ] Final cleanup: `#ifdef` guards removed, `zobrist_undo_stack` eliminated
- [ ] All regression tests pass (Levels 1–5) with final code
- [ ] No runtime performance regression detected

---

## Success Metrics

- **Memory footprint:** Eliminated 10-byte `zobrist_undo` struct × millions of states, reducing per-solve memory usage
- **Code clarity:** Removed undo stack management; inline undo computation is self-contained and easier to reason about
- **Correctness:** Parallel-path assertions provide 100% confidence that inline computation matches reference across all test instances
- **Performance:** Expected to be neutral or slightly faster due to reduced memory allocation/deallocation and better cache locality
- **Maintainability:** Future changes to Zobrist encoding only need updates in one place (inline code), not in both move and undo paths

---

## Related Documentation

- `phase0_plan.md` — Phase 0 (completed)
- `descriptor_undo_analysis.md` — Theoretical foundation for inline undo
- `src/main/game/search-state/game_state.cpp` — Implementation location
- `src/main/game/zobrist.h` — Zobrist hash keys and update methods
