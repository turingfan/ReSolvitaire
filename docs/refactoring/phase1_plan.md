# Phase 1: Incremental Inline Undo Implementation (DESIGN PHASE)

**Date:** 2026-04-10
**Status:** DESIGN IN PROGRESS
**Scope:** Zobrist hash + compact_state payload inline undo. Excludes predecessor/accordion.
**Branch:** `feature/refactoring-phases`
**Prerequisite:** Phase 0 scaffolding complete (see `phase0_plan.md`)
**Related:** `descriptor_undo_analysis.md` (in this directory)

---

## Overview

Phase 1 incrementally replaces each component of `zobrist_undo_stack` with inline computation, one component at a time. Each step is validated exhaustively before proceeding to the next.

At the end of Phase 1, the 10-byte `zobrist_undo` struct and its stack are **eliminated entirely** — no stored undo state is needed. See `descriptor_undo_analysis.md` for the proof that all values, including STARTING_FACE_UP, are recoverable from pile state using the face-down card invariant.

---

## Implementation Strategy

### High-Level Approach

Each step below replaces ONE component of the undo record with inline computation. The pattern for each step is:

1. Write the inline computation code inside the `#ifdef VALIDATE_INLINE_UNDO` block
2. Assert that the inline result matches the undo-stack result
3. Run unit tests + Level 1 regression with the validation build
4. When the assert passes on all tests, the component is proven correct
5. Commit and move to the next component

After all components are validated, the final step replaces the existing undo code with the inline code and removes the undo stack.

### The Dual-State Approach

The key challenge is that the existing undo code modifies hash/payload as it runs, so we can't simply "run both paths." Instead, we use a snapshot approach:

**For each undo function:**
1. Record `pre_undo_hash` and `pre_undo_payload` (the state before ANY undo happens)
2. Run the existing undo code → produces `expected_hash` and `expected_payload`
3. Restore `pre_undo_hash` and `pre_undo_payload` (reset to pre-undo state)
4. Run the new inline code → produces `inline_hash` and `inline_payload`
5. Assert `inline_hash == expected_hash` and `inline_payload.matches(expected_payload)`
6. Leave the state as the existing code left it (so the rest of the solver works)

Step 6 means after validation, we restore to the expected state:
```cpp
zobrist_hash_value = expected_hash;
payload = expected_payload;
```

This approach lets us validate incrementally: as each component is added to the inline path, the assert catches any divergence.

---

## Implementation Strategy (To Be Defined)

**User input needed:** Ian, please describe your idea for Phase 1 implementation.

Key questions to address:
1. What is the granularity of inline implementation? (per-component, per-undo-function, or a different strategy?)
2. What is the implementation order? (which components first?)
3. Are there architectural constraints or simplifications you'd like to apply?
4. How do you want to handle the reveal_move complexity noted in the initial analysis?
5. Any specific testing or validation strategy beyond the dual-state approach?

---

## Open Design Questions

### 1. Factoring Pile Operations

The initial analysis notes that pile operations can be factored out and shared between the undo-stack path and inline path, avoiding redundant computation. Should Phase 1:

- Keep pile operations in the existing path and only inline hash/payload updates?
- Or restructure to explicitly factor out and share pile operations?

**Impact:** Affects code clarity and whether we can validate hash/payload independently from pile state changes.

### 2. Reveal Move Handling

The reveal_move mechanism involves turning cards face-down (`piles[m.from][0].turn_face_down()`) as part of undo. This happens before pile operations in the current code. The inline path needs to:

- Compute the new descriptor for a revealed card (STARTING_FACE_UP → STARTING)
- Know when to apply this (reveal_move flag)
- Handle the interaction with pile state ordering

**Question:** Should reveal undo be a separate component step, or merged with another component?

### 3. Component Ordering

Candidates for inline implementation (in some order):
1. Pile operations (reverse of card movement)
2. Reveal undo (if moving card face-down, update descriptor)
3. Foundation undo (restore old foundation ranks in hash)
4. Hole top undo (restore old hole top card in hash)
5. Waste pointer undo (restore old waste ptr in hash)
6. Moved card descriptor undo (restore old descriptor in payload)

**Question:** What is the natural/optimal order? Should it follow the existing code's order, or a different dependency graph?

### 4. Validation Scope

Currently Phase 0 scaffolding covers:
- `undo_regular_move`
- `undo_built_group_move`
- `undo_stock_k_plus_move`
- `undo_stock_to_all_tableau_move`

**Question:** Should all four be implemented in lockstep, or can they proceed independently?

---

## Placeholder: Implementation Steps

(To be defined based on user input)

### Step 1.0: [Define initial component to inline]

### Step 1.1: [Inline first component]

### Step 1.2: [Inline second component]

... (and so on)

---

## Exit Criteria (To Be Defined)

- [ ] All inline components pass validation tests
- [ ] No runtime performance regression
- [ ] undo_undo_stack can be safely removed (if final step includes cleanup)
- [ ] All regression tests pass
- [ ] Documentation updated to reflect new undo mechanism

---

## Success Metrics

- Reduced memory footprint (10-byte undo struct * millions of states → zero)
- Simplified code path (no undo stack management)
- Maintained correctness (validation harness proves equivalence)
- Reasonable performance (likely neutral or slightly faster due to reduced memory traffic)

---

## Related Documentation

- `phase0_plan.md` — Phase 0 (completed)
- `descriptor_undo_analysis.md` — Theoretical foundation for inline undo
- `src/main/game/search-state/game_state.cpp` — Implementation location
- `src/main/game/zobrist.h` — Zobrist hash keys and update methods
