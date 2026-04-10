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

## Design Decisions & Remaining Questions

### 1. Component Implementation Order

Within each move type, which components should be implemented first? Candidates:

1. **Pile operations** — determine card movement (in/out of piles)
2. **Reveal undo** — if turning card face-down, update descriptor (STARTING_FACE_UP → STARTING)
3. **Foundation undo** — restore old foundation ranks in hash
4. **Hole top undo** — restore old hole top card in hash (if game has holes)
5. **Waste pointer undo** — restore old waste ptr in hash (if game has waste pile)
6. **Moved card descriptor undo** — restore old descriptor in payload

**Considerations:**
- Should order follow the existing code's logic, or a cleaner dependency graph?
- Are some components always required together (e.g., foundation + hole top)?
- Should we implement "easiest first" (highest confidence) or "riskiest first" (to validate early)?

### 2. Reveal Move Handling

The reveal_move mechanism turns a card face-down during undo (`piles[m.from][0].turn_face_down()`). The inline path needs to:

- Compute the new descriptor for a revealed card (STARTING_FACE_UP → STARTING)
- Know when to apply this (reveal_move flag in the move struct)
- Ensure interaction with pile state is correct

**Decision needed:** How should reveal descriptor updates be integrated with the component ordering above?

### 3. Move Type Implementation Order

Currently Phase 0 scaffolding covers four move types:
- `undo_regular_move`
- `undo_built_group_move`
- `undo_stock_k_plus_move`
- `undo_stock_to_all_tableau_move`

**Decision needed:**
- Implement all four in parallel, or tackle them sequentially?
- If sequential, which move type first (simplest, or most-used)?
- If parallel, do they share component implementations, or are they independent?

---

## Implementation Steps (To Be Defined)

Once component and move-type ordering are finalized, the implementation will follow this pattern:

### Step 1.X: Implement component C in move type M

**Process:**
1. Open `undo_M` function in `game_state.cpp`
2. Inside the `#ifdef VALIDATE_INLINE_UNDO` block, add inline computation for component C
3. For any components not yet implemented in this move type, keep the fallback copy: `inline_hash = expected_hash;` etc.
4. Add assertion: `assert(inline_hash == expected_hash && "...")` and `assert(inline_payload.matches(expected_payload) && "...")`
5. Compile with `-DVALIDATE_INLINE_UNDO=ON`
6. Run unit tests + Level 1 regression
7. Commit with message: `feat: inline component C for move type M`

### Step 1.Y: Implement component D in move type M

Repeat the process above for the next component in the same move type.

### Step 1.Z: Switch to next move type

Once all components for move type M are implemented and passing, move to the next move type and repeat.

### Final Step: Clean-up and remove undo stack

Once all components for all move types are validated:
1. Remove `#ifdef VALIDATE_INLINE_UNDO` guards (keep inline code, remove assertions and reference copies)
2. Remove `zobrist_undo_stack` and related cleanup code
3. Run full regression suite (Levels 1–5)
4. Commit with message: `refactor: eliminate zobrist_undo_stack, inline all undo computation`

---

## Exit Criteria

- [ ] Inline computation implemented for all components in all four move types
- [ ] `#ifdef VALIDATE_INLINE_UNDO` assertions pass on all unit tests
- [ ] Level 1 regression passes with validation enabled
- [ ] Level 2 regression passes with validation enabled (longer test suite)
- [ ] `zobrist_undo_stack` removed and cleanup code eliminated
- [ ] Assertions removed, `#ifdef` guards stripped from final code
- [ ] No runtime performance regression (measured via Level 2+ regression timeouts)
- [ ] All regression tests pass (Levels 1–5)

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
