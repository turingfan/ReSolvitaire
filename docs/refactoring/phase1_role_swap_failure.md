# Phase 1 Role Swap Attempt: Failure Report

**Date:** 2026-04-10
**Status:** FAILED - Fundamental architectural mismatch
**Branch:** `phase1-analysis`
**Commit:** `3a70f64`

---

## Executive Summary

Attempted to swap the computational roles in undo functions:
- **Goal:** Inline path becomes PRIMARY (updates state), old path becomes VALIDATION
- **Result:** Implementation passes all tests but is architecturally broken
- **Issue:** Still uses undo_stack throughout despite claiming to eliminate it

---

## What Was Attempted

Make inline undo computation from Phase 1 the primary path while keeping the old undo_stack approach as validation:

1. Move inline helper function definitions outside `#ifdef VALIDATE_INLINE_UNDO` (make always available)
2. In each undo function: compute inline hash/payload first
3. Update `zobrist_hash_value` and `payload` with inline results
4. Run old path on copy, verify it matches inline results
5. Tests pass = role swap successful

## What Actually Happened

The implementation is **fundamentally broken**:

```cpp
// Inline path runs and updates global state
zobrist_hash_value = inline_hash;
payload = inline_payload;

// Then old helpers run and ALSO update global state
update_card_descriptor(undo.card_id, undo.old_desc);      // Modifies zobrist_hash_value
update_foundation_in_hash(undo.from_found_suit, ...);    // Modifies zobrist_hash_value
update_waste_ptr_in_hash(undo.old_waste_ptr);            // Modifies zobrist_hash_value
// ... more helpers that modify state ...

// Then we copy inline values back
zobrist_hash_value = inline_hash;
payload = inline_payload;
```

**This is NOT a role swap.** The old undo_stack path is still active. I'm just:
1. Computing inline
2. Computing old (which overwrites inline)
3. Copying inline back

Result: **Undo_stack is not eliminated, just masked by value reversion.**

---

## Why Tests Passed

All 234 unit tests passed, plus Level 1 and Level 2 regression. This created false confidence.

**Why:** Both paths produce identical results (as proven by Phase 0 metamorphic testing), so the final state is correct even though the architecture is wrong.

**What tests did NOT check:**
- Whether old undo_stack calls were actually eliminated
- Whether inline path could function without the old helpers running
- Whether undo_stack values were consumed in the "primary" path

---

## Root Causes

### 1. Misunderstanding of "Role Swap"

I interpreted as: "Run both, use inline result as primary"

Likely intended: "Eliminate old path, use inline alone"

These require completely different architectures.

### 2. Dependency on Undo Stack Values

Both inline and old paths need `undo.old_desc`, `undo.old_to_found_rank`, etc.:

```cpp
// Inline path uses undo record:
update_inline_card_descriptor(undo.card_id, undo.old_desc, inline_hash, inline_payload);

// Old path also uses undo record:
update_card_descriptor(undo.card_id, undo.old_desc);
```

The undo record is not eliminated, it's used by both paths. The inline path cannot replace the old path as long as both need the same undo information.

### 3. Conflation of Hash Computation with State Recovery

- **Hash computation:** Inline can compute XOR deltas independently ✓
- **State recovery:** Both paths need to know WHAT values to restore (from undo record) ✗

The inline path successfully computes that a descriptor changed from X → Y, but it still needs the undo record to know what Y should be.

---

## Critical Questions About Phase 0

Phase 0 metamorphic testing proved:
- `inline_hash == old_path_hash` ✓
- `inline_payload == old_path_payload` ✓

But did NOT prove:
- Can inline path work WITHOUT the old path?
- Can inline path work WITHOUT the undo record?
- Is inline computation truly independent?

**Hypothesis:** Phase 0 was checking "both paths compute the same deltas when given the same input," not "inline path is sufficient on its own."

The parallel computation with fallback copies proved equivalence, not independence.

---

## Specific Areas of Concern

### A. Can Inline Path Truly Be Primary? (Critical)

The inline path needs values from `undo` struct to know what the old state was:

```cpp
update_inline_card_descriptor(undo.card_id, undo.old_desc, inline_hash, inline_payload);
```

This uses `undo.old_desc` to compute the descriptor change. Can this value be recovered from post-undo pile state alone?

**Questions to answer:**
1. Is `undo.old_desc` recoverable from `payload.get_descriptor(undo.card_id)` after pile operations?
2. If yes, why did Phase 0 need to store it in the undo_stack?
3. If no, the undo_stack cannot be eliminated at the undo stage.

### B. What Did Phase 0 Actually Validate? (Critical)

The metamorphic testing added inline code alongside existing code:

```cpp
#ifdef VALIDATE_INLINE_UNDO
    // Compute inline on a COPY of hash/payload
    uint64_t inline_hash = expected_hash;
    // ... compute inline ...
    // Assert inline == expected
#endif
```

This proved both compute the same result, but:
- Both had access to the same undo record
- Both ran in the same scope with the same information
- Neither was isolated or forced to be independent

**Question:** Would the inline path work if the undo_stack didn't exist at all?

### C. Reveal Move State Semantics (Architecture)

When I reordered operations (pile operations before/after reveal descriptor update), tests broke. This suggests:

1. **Physical state (face-up/face-down bit) and logical state (descriptor) are tightly coupled**
2. **The order of operations matters beyond just hash computation**
3. **The inline path may be relying on side effects from pile operations that occur at a specific time**

**Question:** Is the inline reveal handling correct, or does it work only because the old path also runs and enforces consistency?

---

## Issues with My Behavior & Approach

### 1. Insufficient Clarification of Requirements

- Did not ask what "role swap" meant architecturally
- Did not ask for acceptance criteria (e.g., "undo_stack must be deleted")
- Assumed test passing = success without validating architectural intent

**Better:** Request explicit acceptance criteria:
```
The inline path:
[ ] Does not call any function consuming undo record
[ ] Does not use zobrist_undo_stack values
[ ] Can be compiled/run with zobrist_undo_stack deleted entirely
[ ] Computes state changes from board state alone
```

### 2. Over-Reliance on Test Results

- Tests passed → declared success
- Did not verify that old code was actually eliminated
- Did not check for uses of `undo.` in "primary" path

**Better:** Require architectural validation before deeming implementation correct:
```bash
grep -n "undo\." src/main/game/search-state/game_state.cpp | grep -v "zobrist_undo_stack.pop"
# Should return ZERO results in primary code paths
```

### 3. Misinterpreting Test Failure Then Fixing Wrong Problem

- Test failed when I reordered operations
- I fixed the order, tests passed
- Never questioned whether the design itself was flawed
- Masked the real issue (still using undo_stack) with a symptom fix

**Better:** When tests fail, investigate:
1. Is this a *symptom* of a deeper design flaw?
2. Does my fix actually eliminate the root cause, or mask it?
3. Would this fix pass tests if the old code was deleted?

### 4. Not Reading My Own Code Critically

The validation path contains:
```cpp
update_card_descriptor(undo.card_id, undo.old_desc);
update_foundation_in_hash(undo.to_found_suit, undo.old_to_found_rank);
update_waste_ptr_in_hash(undo.old_waste_ptr);
```

**Red flags I missed:**
- Still consuming undo record in "validation" path
- Still calling old helper functions that modify global state
- Still reverting global state afterward (suggests wrong design)

**Better:** Add explicit checks:
```
For each undo function:
[ ] Identify all uses of undo.* — categorize as primary vs validation
[ ] Identify all state-modifying calls — verify they're in correct path
[ ] Check for any reversion of global state (if yes, design is wrong)
```

---

## Recommendations for Future Work

### When Given Similar Tasks:

1. **Ask for Acceptance Criteria BEFORE Starting**
   ```
   "What does 'done' mean?"
   - All tests pass?
   - Undo_stack eliminated?
   - Old path deleted?
   - Inline path stands alone?
   ```

2. **Establish Architectural Validation Gates**
   ```
   Before testing: Verify architecture
   - Grep for uses of eliminated components
   - Confirm dependencies are gone
   - Validate that primary path is truly independent
   ```

3. **Use Staged Validation**
   ```
   1. Code review (does it match intent?)
   2. Architectural check (does it use eliminated components?)
   3. Test execution (do tests pass?)
   4. Regression check (does removing old code break anything?)
   ```

4. **Flag Suspicious Patterns in Your Own Code**
   When you see:
   - Uses of undo record values in supposedly independent code
   - State being modified then reverted
   - "Validation" paths that consume the same resources as primary

   **Stop and ask:** Is this the right design?

5. **Distinguish Between:**
   - **Computational equivalence:** Both paths compute the same result ✓
   - **Architectural independence:** One path works without the other ✗ (What I missed)

---

## Timeline of Events

1. **Start:** Branch from b6b9100 (both inline and old paths intact)
2. **Test 1:** Confirm tests pass without validation, with validation, both + L1/L2 regression ✓
3. **Attempt:** Move inline helpers outside #ifdef, restructure to swap roles
4. **Issue:** FaceUpCards test fails
5. **Response:** Fix operation order (wrong diagnosis)
6. **Result:** Tests pass, declare success
7. **Review:** Realize I'm still using undo_stack throughout
8. **Conclusion:** Design is broken, not implementation

---

## What Should Be Investigated

For a higher-level AI or domain expert to examine:

1. **Recovery of Undo Values from Pile State**
   - Can `undo.old_desc` be recovered from post-undo `payload.get_descriptor(card_id)`?
   - Can `undo.old_foundation_rank` be recovered from post-undo pile state?
   - If yes → inline can work alone. If no → undo_stack is necessary.

2. **Phase 0 Assumptions**
   - Did Phase 0 prove inline path can work alone, or just that it computes the same values?
   - Review the actual assertions and what they validate
   - Consider whether phase 0 testing was checking the right thing

3. **Alternatives to Role Swap**
   - Can inline computation be used to *verify* old path (opposite direction)?
   - Can inline computation be computed post-undo to double-check hash consistency?
   - Is the goal actually to eliminate undo_stack, or to validate it?

4. **Architectural Constraints**
   - Is there a fundamental reason both paths need the undo record?
   - Can the inline path be made truly independent?
   - Should the goal be "eliminate undo_stack" or "use inline as verification"?

---

## Conclusion

The attempt to swap computational roles appears successful (tests pass) but is architecturally flawed (still uses undo_stack). This is a subtle failure mode where a broken design can pass tests if those tests don't validate the actual architecture.

**Root cause:** Misunderstanding of requirements + insufficient architectural validation.

**Do not attempt to fix this implementation.** The fix requires clarity on whether inline undo can actually work as a standalone system, or whether the undo_stack is a fundamental requirement that cannot be eliminated at the undo stage.

---

**Report prepared for:** Higher-level AI/domain expert to investigate foundational assumptions about Phase 1's inline undo approach.
