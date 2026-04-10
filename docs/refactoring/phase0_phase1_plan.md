# Phase 0 & Phase 1 Implementation Plan (Compact State Only)

**Date:** 2026-04-10
**Scope:** Zobrist hash + compact_state payload inline undo. Excludes predecessor/accordion.
**Branch:** `feature/refactoring-phases`
**Prerequisite reading:** `descriptor_undo_analysis.md` (in this directory)

---

## Overview

**Phase 0** sets up metamorphic testing infrastructure so that every change in Phase 1 is validated against the existing code as oracle.

**Phase 1** incrementally replaces each component of `zobrist_undo_stack` with inline computation, one component at a time. Each step is validated exhaustively before proceeding to the next.

At the end of Phase 1, the 10-byte `zobrist_undo` struct and its stack are **eliminated entirely** — no stored undo state is needed. See `descriptor_undo_analysis.md` for the proof that all values, including STARTING_FACE_UP, are recoverable from pile state using the face-down card invariant.

---

## Phase 0: Metamorphic Testing Infrastructure

### Goal

Build a test harness that runs the solver with **dual computation**: the existing undo-stack path computes hash and payload values, and a new inline path computes the same values independently. After each undo operation, the harness asserts that both paths produce bit-identical results.

This harness must be in place and passing BEFORE any Phase 1 code is written.

### Step 0.1: Add `#ifdef VALIDATE_INLINE_UNDO` scaffolding

Add a preprocessor flag `VALIDATE_INLINE_UNDO` that, when defined, enables validation code in each `undo_*_move` function. Initially the validation code does nothing — it just saves and restores hash/payload values to prove the scaffolding works.

**File:** `src/main/game/search-state/game_state.cpp`

Add to each `undo_*_move` function (except `undo_sequence_move` and `undo_accordion_move` which are out of scope):

```cpp
void game_state::undo_regular_move(const move m) {
#ifdef VALIDATE_INLINE_UNDO
    // Snapshot state BEFORE old undo path runs
    uint64_t expected_hash;
    compact_state expected_payload;
#endif

    // === EXISTING UNDO CODE (unchanged) ===
    zobrist_undo undo = zobrist_undo_stack.back();
    zobrist_undo_stack.pop_back();
    // ... (all existing undo logic) ...
    place_card(m.from, take_card(m.to));

#ifdef VALIDATE_INLINE_UNDO
    // Snapshot state AFTER old undo path — this is the expected result
    expected_hash = zobrist_hash_value;
    expected_payload = payload;

    // === NEW INLINE UNDO CODE (to be filled in Phase 1 steps) ===
    // For now: no-op placeholder
    uint64_t inline_hash = expected_hash;       // placeholder
    compact_state inline_payload = expected_payload; // placeholder

    // === VALIDATE ===
    assert(inline_hash == expected_hash
        && "INLINE UNDO: hash mismatch");
    assert(inline_payload.matches(expected_payload)
        && "INLINE UNDO: payload mismatch");
#endif
}
```

Repeat for `undo_built_group_move`, `undo_stock_k_plus_move`, `undo_stock_to_all_tableau_move`.

**Build command:**
```bash
./build.sh --release --unit-tests -DVALIDATE_INLINE_UNDO
# or via cmake:
cmake -DCMAKE_CXX_FLAGS="-DVALIDATE_INLINE_UNDO" ...
```

**Implementation notes:**
- The `#ifdef` block must appear AFTER the existing undo code runs (so pile state and hash are already restored)
- The new inline code will gradually be filled in during Phase 1 steps
- The placeholders ensure the scaffolding compiles and the asserts pass trivially

### Step 0.2: Add CMake option for validation build

**File:** `CMakeLists.txt`

Add an option:
```cmake
option(VALIDATE_INLINE_UNDO "Enable dual-path Zobrist undo validation" OFF)
if(VALIDATE_INLINE_UNDO)
    add_definitions(-DVALIDATE_INLINE_UNDO)
endif()
```

Also add a new CTest target:
```cmake
add_test(NAME validation_build_unit_tests
    COMMAND ${CMAKE_BINARY_DIR}/unit_tests --gtest_filter=-DualCacheTest*:MismatchDiagnostic*
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

### Step 0.3: Verify scaffolding passes all existing tests

Run:
```bash
cd cmake-build-release
cmake -DVALIDATE_INLINE_UNDO=ON ..
make -j$(nproc)
ctest -R unit_tests --output-on-failure
ctest -R regression_level1 --output-on-failure
```

All tests must pass. The validation asserts are trivially satisfied by the placeholders.

### Step 0.4: Add a validation-specific regression test

Add a CTest entry that runs Level 1 regression with the validation build. This is the test that will catch Phase 1 bugs:

```cmake
if(VALIDATE_INLINE_UNDO)
    add_test(NAME validation_regression_level1
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/regression_runner.py
            --solver ${CMAKE_BINARY_DIR}/solvitaire
            --oracle ${CMAKE_SOURCE_DIR}/tests/oracles/level1_oracle.json
            --level 1
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(validation_regression_level1 PROPERTIES TIMEOUT 300)
endif()
```

### Phase 0 Exit Criteria

- [ ] `VALIDATE_INLINE_UNDO` builds without warnings
- [ ] All unit tests pass with the flag enabled
- [ ] Level 1 regression passes with the flag enabled
- [ ] Scaffolding is in all 4 undo functions (regular, built_group, stock_k_plus, stock_to_all_tableau)

---

## Phase 1: Incremental Inline Undo Implementation

### Architecture

Each step below replaces ONE component of the undo record with inline computation. The pattern for each step is:

1. Write the inline computation code inside the `#ifdef VALIDATE_INLINE_UNDO` block
2. Assert that the inline result matches the undo-stack result
3. Run unit tests + Level 1 regression with the validation build
4. When the assert passes on all tests, the component is proven correct
5. Commit and move to the next component

After all components are validated, the final step replaces the existing undo code with the inline code and removes the undo stack.

### Implementation Strategy: The Dual-State Approach

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

### Detailed scaffolding template

Here is the exact scaffolding for `undo_regular_move`. The other undo functions follow the same pattern.

```cpp
void game_state::undo_regular_move(const move m) {
#ifdef VALIDATE_INLINE_UNDO
    // Snapshot pre-undo state
    uint64_t pre_hash = zobrist_hash_value;
    compact_state pre_payload = payload;
#endif

    // === EXISTING UNDO CODE (unchanged) ===
    zobrist_undo undo = zobrist_undo_stack.back();
    zobrist_undo_stack.pop_back();

    if (m.reveal_move) {
        update_card_descriptor(undo.revealed_card_id, compact_state::STARTING);
        piles[m.from][0].turn_face_down();
    }
    if (undo.old_hole_top != 255)
        update_hole_top_in_hash(undo.old_hole_top);
    if (undo.old_waste_ptr != 255)
        update_waste_ptr_in_hash(undo.old_waste_ptr);
    if (undo.to_found_suit != 255)
        update_foundation_in_hash(undo.to_found_suit, undo.old_to_found_rank);
    if (undo.from_found_suit != 255)
        update_foundation_in_hash(undo.from_found_suit, undo.old_from_found_rank);
    update_card_descriptor(undo.card_id, undo.old_desc);
    place_card(m.from, take_card(m.to));

#ifdef VALIDATE_INLINE_UNDO
    // Snapshot expected result
    uint64_t expected_hash = zobrist_hash_value;
    compact_state expected_payload = payload;

    // Restore to pre-undo state for inline path
    zobrist_hash_value = pre_hash;
    payload = pre_payload;

    // === INLINE UNDO PATH ===
    // (Steps 1.1–1.6 will progressively fill this in)

    // Step 1.1: Pile operations first
    // (To be added)

    // Step 1.2: Reveal undo
    // (To be added)

    // Step 1.3: Foundation undo
    // (To be added)

    // Step 1.4: Hole top undo
    // (To be added)

    // Step 1.5: Waste pointer undo
    // (To be added)

    // Step 1.6: Moved card descriptor undo
    // (To be added)

    // PLACEHOLDER: copy expected result (passes trivially)
    zobrist_hash_value = expected_hash;
    payload = expected_payload;

    // === VALIDATE ===
    assert(zobrist_hash_value == expected_hash
        && "INLINE UNDO: hash mismatch in undo_regular_move");
    assert(payload.matches(expected_payload)
        && "INLINE UNDO: payload mismatch in undo_regular_move");
#endif
}
```

---

### Step 1.0: Establish the dual-state scaffolding

Replace the Phase 0 placeholder scaffolding with the full dual-state approach shown above. At this point, the inline path still copies the expected result (so asserts pass trivially).

**Files changed:** `game_state.cpp` (4 undo functions)

**Validation:**
```bash
cmake -DVALIDATE_INLINE_UNDO=ON .. && make -j$(nproc)
ctest -R unit_tests --output-on-failure
ctest -R regression_level1 --output-on-failure
```

**Exit criteria:** All tests pass. This proves the snapshot/restore mechanism works correctly.

---

### Step 1.1: Inline pile operations for `undo_regular_move`

**What changes:** In the inline undo path, perform pile operations first (reverse of make_move):

```cpp
// === INLINE UNDO PATH ===

// Step 1.1: Pile operations first (reverse of make_regular_move)
place_card(m.from, take_card(m.to));

// Undo reveal (pile state level only)
if (m.reveal_move) {
    // After pile undo, revealed card is at piles[m.from][1]
    // (below the returned card at piles[m.from][0])
    // But we haven't turned it face-down yet — that's a later step
    // For now, just note that the pile ops are done
}

// PLACEHOLDER: copy expected hash/payload (other components not yet inline)
zobrist_hash_value = expected_hash;
payload = expected_payload;
```

**Wait — there's a problem.** The existing undo code already did the pile operations (the last line of the existing code is `place_card(m.from, take_card(m.to))`). If the inline path also does pile operations, we'd be doing them twice.

**Fix:** The pile operations in the inline path must UNDO the existing code's pile operations first (put the card back at m.to), then redo them for the inline path. But this is wasteful and confusing.

**Better approach:** The pile operations are the SAME in both paths. Only the hash/payload updates differ. So restructure:

```cpp
void game_state::undo_regular_move(const move m) {
#ifdef VALIDATE_INLINE_UNDO
    uint64_t pre_hash = zobrist_hash_value;
    compact_state pre_payload = payload;
#endif

    // === EXISTING HASH/PAYLOAD UNDO (without pile ops) ===
    zobrist_undo undo = zobrist_undo_stack.back();
    zobrist_undo_stack.pop_back();
    if (m.reveal_move) {
        update_card_descriptor(undo.revealed_card_id, compact_state::STARTING);
        piles[m.from][0].turn_face_down();
    }
    if (undo.old_hole_top != 255)
        update_hole_top_in_hash(undo.old_hole_top);
    if (undo.old_waste_ptr != 255)
        update_waste_ptr_in_hash(undo.old_waste_ptr);
    if (undo.to_found_suit != 255)
        update_foundation_in_hash(undo.to_found_suit, undo.old_to_found_rank);
    if (undo.from_found_suit != 255)
        update_foundation_in_hash(undo.from_found_suit, undo.old_from_found_rank);
    update_card_descriptor(undo.card_id, undo.old_desc);

    // === PILE OPERATIONS (shared — only done once) ===
    place_card(m.from, take_card(m.to));

#ifdef VALIDATE_INLINE_UNDO
    uint64_t expected_hash = zobrist_hash_value;
    compact_state expected_payload = payload;

    // Restore hash/payload to pre-undo state (pile state stays as-is — already undone)
    zobrist_hash_value = pre_hash;
    payload = pre_payload;

    // === INLINE HASH/PAYLOAD UNDO ===
    // The piles are already in the post-undo state (pile ops were shared above).
    // Now compute hash/payload updates from the restored pile state.

    // ... (Steps 1.2–1.6 fill this in) ...

    // PLACEHOLDER: copy expected (passes trivially)
    zobrist_hash_value = expected_hash;
    payload = expected_payload;

    assert(zobrist_hash_value == expected_hash);
    assert(payload.matches(expected_payload));
#endif
}
```

**Key insight:** Pile operations are factored out and shared between both paths. The validation only covers hash/payload computation. This is cleaner and avoids double pile ops.

**BUT** there is a complication: the existing undo code handles the reveal's `piles[m.from][0].turn_face_down()` BEFORE pile ops. In the inline path (pile-first), the reveal undo would happen AFTER pile ops. The `turn_face_down()` call is a pile mutation.

**Resolution:** The existing code's ordering is:
1. Turn revealed card face-down (pile mutation)
2. Update hash/payload for reveal
3. Update other hash/payload components
4. Pile ops (move card back)

The inline path's ordering would be:
1. Pile ops (move card back)
2. Turn revealed card face-down (pile mutation)
3. Update hash/payload for all components

Since reveal face-down is a pile mutation (it changes `piles[m.from][1].is_face_down()`), it MUST happen before certain hash/payload computations and after others. This means the shared pile-ops approach needs careful handling.

**Simplest clean approach:** In the validation path, we need to undo BOTH the pile ops AND the reveal face-down from the existing code, then redo them in inline order. This gets messy.

**Cleanest approach: Run the inline path FIRST, before the existing code.**

```cpp
void game_state::undo_regular_move(const move m) {
#ifdef VALIDATE_INLINE_UNDO
    // === INLINE PATH (runs first, on unmodified state) ===
    uint64_t pre_hash = zobrist_hash_value;
    compact_state pre_payload = payload;

    // Pile operations first
    place_card(m.from, take_card(m.to));

    // Now pile state is restored. Hash/payload still in post-move state.
    // Compute inline hash/payload updates here...
    // (Steps 1.2–1.6)

    // Handle reveal undo
    if (m.reveal_move) {
        // Revealed card is at piles[m.from][1] (below returned card)
        // Turn face-down and update descriptor
        // ...
    }

    // ... other inline updates ...

    uint64_t inline_hash = zobrist_hash_value;
    compact_state inline_payload = payload;

    // Restore to pre-undo state (undo the pile ops and reveal undo we just did)
    // This requires reversing: put card back at m.to, turn reveal back face-up
    if (m.reveal_move) {
        piles[m.from][1].turn_face_up(); // reverse our face-down
    }
    place_card(m.to, take_card(m.from)); // reverse our pile ops
    zobrist_hash_value = pre_hash;
    payload = pre_payload;
#endif

    // === EXISTING UNDO CODE (unchanged) ===
    zobrist_undo undo = zobrist_undo_stack.back();
    zobrist_undo_stack.pop_back();
    if (m.reveal_move) {
        update_card_descriptor(undo.revealed_card_id, compact_state::STARTING);
        piles[m.from][0].turn_face_down();
    }
    if (undo.old_hole_top != 255)
        update_hole_top_in_hash(undo.old_hole_top);
    if (undo.old_waste_ptr != 255)
        update_waste_ptr_in_hash(undo.old_waste_ptr);
    if (undo.to_found_suit != 255)
        update_foundation_in_hash(undo.to_found_suit, undo.old_to_found_rank);
    if (undo.from_found_suit != 255)
        update_foundation_in_hash(undo.from_found_suit, undo.old_from_found_rank);
    update_card_descriptor(undo.card_id, undo.old_desc);
    place_card(m.from, take_card(m.to));

#ifdef VALIDATE_INLINE_UNDO
    // Validate
    assert(inline_hash == zobrist_hash_value
        && "INLINE UNDO: hash mismatch in undo_regular_move");
    assert(inline_payload.matches(payload)
        && "INLINE UNDO: payload mismatch in undo_regular_move");
#endif
}
```

**This is the correct architecture:** The inline path runs first on unmodified state, captures results, then fully reverses itself. The existing code runs normally. We compare results at the end.

The reversal is simple: undo the pile ops and face-down toggle. Hash/payload are restored from the snapshot.

**Exit criteria:** All tests pass with the inline path doing pile ops + reversal + placeholder hash/payload (copied from pre-state, which means the assert will fail unless we copy from expected).

Actually, for step 1.0 the inline path should just capture `pre_hash`/`pre_payload` and the assert should compare against the existing code's final state. Since the inline path hasn't computed anything yet, use the PLACEHOLDER approach:

```cpp
// PLACEHOLDER: For now, set inline results to existing code's results
uint64_t inline_hash = zobrist_hash_value;  // after existing code runs
compact_state inline_payload = payload;
// assert passes trivially
```

---

### Step 1.1: Inline reveal undo for `undo_regular_move`

**What we implement:** In the inline path (after pile undo), identify the revealed card and update its descriptor.

```cpp
// In the inline path, after pile undo:
if (m.reveal_move) {
    // After pile undo, moved card is at piles[m.from][0],
    // revealed card is at piles[m.from][1]
    assert(piles[m.from].size() >= 2);
    assert(!piles[m.from][1].is_face_down()); // still face-up (not yet reverted)

    card rev = piles[m.from][1];
    uint8_t rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());

    // Revealed card's pre-reveal descriptor was always STARTING
    update_card_descriptor(rev_cid, compact_state::STARTING);

    // Turn face-down (pile mutation)
    piles[m.from][1].turn_face_down();
}
```

**Key point:** The revealed card is at index 1 (not 0) because the moved card has been placed back on top.

**Validation:**
```bash
cmake -DVALIDATE_INLINE_UNDO=ON .. && make -j$(nproc)
ctest -R unit_tests --output-on-failure
ctest -R regression_level1 --output-on-failure
```

**What to check if assert fires:** The revealed card's identity. Compare `rev_cid` against `undo.revealed_card_id` from the existing path. If they differ, the indexing is wrong.

**Exit criteria:** Level 1 passes. The reveal component now matches the existing code for all 150 test instances.

---

### Step 1.2: Inline foundation undo for `undo_regular_move`

**What we implement:** After pile undo, read foundation top ranks from restored pile state.

```cpp
// In the inline path, after pile undo:

// Undo destination foundation
if (is_foundation_pile(m.to)) {
    uint8_t to_fs = get_foundation_suit(m.to);
    // After pile undo, card removed from m.to. New top = old pre-move top.
    uint8_t old_to_fr = piles[m.to].empty()
        ? uint8_t(0) : piles[m.to].top_card().get_rank();
    update_foundation_in_hash(to_fs, old_to_fr);
}

// Undo source foundation
if (is_foundation_pile(m.from)) {
    uint8_t from_fs = get_foundation_suit(m.from);
    // After pile undo, card placed back at m.from. New top = the moved card = old pre-move top.
    uint8_t old_from_fr = piles[m.from].top_card().get_rank();
    update_foundation_in_hash(from_fs, old_from_fr);
}
```

**Note on ordering:** The existing code undoes destination foundation BEFORE source foundation. The inline path should maintain the same XOR ordering for hash consistency. Since XOR is commutative, the order doesn't matter for the final hash value, but the intermediate payload values (set_foundation) must end up at the same values.

**Validation:** Same as Step 1.1. Run unit tests + Level 1 regression.

**What to check if assert fires:** Compare `old_to_fr` against `undo.old_to_found_rank` and `old_from_fr` against `undo.old_from_found_rank`.

**Exit criteria:** Level 1 passes. Foundation component matches.

---

### Step 1.3: Inline hole top undo for `undo_regular_move`

**What we implement:** After pile undo, read hole top from restored pile state.

```cpp
// In the inline path, after pile undo:
if (m.to == hole) {
    // After pile undo, card removed from hole. Top = old pre-move hole top.
    uint8_t old_ht;
    if (piles[hole].empty()) {
        old_ht = 0;  // Default empty hole sentinel (matches init_payload_and_hash)
    } else {
        card ht = piles[hole].top_card();
        old_ht = zobrist_hash::card_id(ht.get_suit(), ht.get_rank());
    }
    update_hole_top_in_hash(old_ht);
}
```

**Risk:** The empty-hole sentinel. The existing code uses `old_ht` captured before the move; if the hole was empty, `old_ht = payload.get_hole_top()` which is whatever was in the payload byte (0 after init). We must verify that reading from the pile state (empty → 0) matches the payload's stored value.

Check: `init_payload_and_hash()` sets `payload.set_hole_top(cid)` when hole is non-empty. When hole is empty, the byte defaults to 0 from `payload.clear()`. So `0` is correct for empty hole.

**Validation:** Run unit tests + Level 1 + Level 2 (Level 2 includes hole games like Golf, Canfield).

**Exit criteria:** Level 2 passes.

---

### Step 1.4: Inline waste pointer undo for `undo_regular_move`

**What we implement:** After pile undo, compute waste pointer from restored waste pile.

```cpp
// In the inline path, after pile undo:
if (m.from == waste) {
    // After pile undo, card placed back at waste. Waste size = pre-move size.
    update_waste_ptr_in_hash(effective_waste_ptr());
}
```

**This is the simplest component.** `effective_waste_ptr()` is a pure function of waste pile size and game rules.

**Validation:** Run unit tests + Level 1. The waste pointer is exercised by Klondike and Canfield seeds.

**Exit criteria:** Level 1 passes.

---

### Step 1.5: Inline moved card descriptor undo for `undo_regular_move`

**What we implement:** Compute the moved card's pre-move descriptor from restored pile state. This uses the face-down card invariant discovered in `descriptor_undo_analysis.md`: a face-up card directly above a face-down card was revealed in place (STARTING_FACE_UP), because moves can only place cards on face-up tops or empty piles.

```cpp
// In the inline path, after pile undo AND reveal undo:
card moved = piles[m.from].top_card();
uint8_t cid = zobrist_hash::card_id(moved.get_suit(), moved.get_rank());

// Determine old descriptor using the face-down invariant
uint8_t old_desc;
if (piles[m.from].size() >= 2 && piles[m.from][1].is_face_down()) {
    // Face-down card below → this card was revealed here, not moved here
    old_desc = compact_state::STARTING_FACE_UP;
} else {
    // Normal case: compute from pile context
    old_desc = determine_destination_descriptor(m.from, moved);
}

update_card_descriptor(cid, old_desc);
```

**IMPORTANT: Ordering constraint.** The face-down check MUST happen AFTER reveal undo (Step 1.1). If this move had `reveal_move=true`, the reveal undo turns `piles[m.from][1]` face-down. That card is the one revealed during THIS move — it's a different card from the moved card. The face-down check for the MOVED card looks at `piles[m.from][1]` which, after reveal undo, reflects the pre-move state. The moved card's pile context is:
- `piles[m.from][0]` = the moved card (returned by pile undo)
- `piles[m.from][1]` = what was below the moved card before the move

If THIS move had `reveal_move=true`, then `piles[m.from][1]` was the card that got revealed (it's now face-down again after reveal undo). But that card was BELOW the moved card originally — the moved card was on TOP of a face-up card before THIS move. Wait — if `reveal_move=true` for the CURRENT move, the card at index 1 (after pile undo) was face-down BEFORE the current move. But the moved card was on top of it at index 0, face-up. That means... the moved card was sitting on a face-down card. So `piles[m.from][1].is_face_down()` would be true, and we'd assign STARTING_FACE_UP.

**But is this correct?** If the current move has `reveal_move=true`, then:
- Before this move: `piles[m.from] = [moved_card(face-up), card_below(face-down), ...]`
- `turn_face_down_cards` set `reveal_move=true` because `piles[m.from][1].is_face_down()` was true
- The moved card IS sitting on a face-down card → it WAS revealed here → STARTING_FACE_UP is correct!

Actually wait. The moved card could have been MOVED here previously and placed on top of a card that was THEN face-up but has SINCE been turned face-down by... no. Cards are never turned face-down during normal play. They're only turned face-down during undo of reveals. The face-down status is set during init and preserved until reveal.

**So the invariant holds.** If `piles[m.from][1].is_face_down()` after full undo, the moved card was revealed in place. STARTING_FACE_UP is correct.

**Validation:** Compare against `undo.old_desc` from the existing undo stack:

```cpp
// Diagnostic: verify our computation matches the oracle
assert(old_desc == undo.old_desc
    && "INLINE UNDO: descriptor reconstruction failed");
```

Run unit tests + Level 1 + Level 2.

**What to check if assert fires:** Log `old_desc`, `undo.old_desc`, `piles[m.from][1].is_face_down()`, and the card identities. This would indicate either the face-down invariant is violated for some game type, or the ordering of reveal undo vs. descriptor computation is wrong.

**Exit criteria:** Level 2 passes with zero assertion failures. This proves the face-down invariant holds across all tested game types.

---

### Step 1.6: Repeat Steps 1.1–1.5 for `undo_built_group_move`

The built_group undo follows the same pattern but with differences:

**Pile undo:** Return `m.count` cards from `m.to` to `m.from`:
```cpp
for (auto pile_idx = m.count; pile_idx-- > 0;) {
    place_card(m.from, piles[m.to][pile_idx]);
}
for (uint8_t rem_count = 0; rem_count < m.count; rem_count++) {
    take_card(m.to);
}
```

**Reveal:** The revealed card is at `piles[m.from][m.count]` after pile undo (below the returned group), not at `piles[m.from][1]`.

**Foundation/hole/waste:** Not applicable for built_group moves (the undo record has 255 for all these fields).

**Moved card descriptor:** The bottom card of the group is the one whose descriptor changes. After pile undo, it's at `piles[m.from][m.count - 1]`.

**Validation:** Same pattern. Run unit tests + Level 1.

---

### Step 1.7: Repeat Steps 1.1–1.5 for `undo_stock_k_plus_move`

The stock_k_plus undo has more complex pile operations:

**Pile undo (in reverse order of make_move):**
```cpp
// 1. Undo flip_waste (if applicable)
if (m.flip_waste) {
    while (!piles[stock].empty()) {
        place_card(waste, take_card(stock));
    }
}
// 2. Undo play to destination
place_card(waste, take_card(m.to));
// 3. Undo deal from stock
if (m.count > 0) {
    for (int i = 0; i < m.count; i++) {
        place_card(stock, take_card(waste));
    }
} else {
    for (int i = 0; i > m.count; i--) {
        place_card(waste, take_card(stock));
    }
}
```

**Foundation/hole:** Same as regular move — read from pile state after undo.

**Waste pointer:** After all pile undo ops, `effective_waste_ptr()` returns the pre-move value.

**Moved card descriptor:** The played card (from waste to destination). Same 1-byte store needed.

**Validation:** Run unit tests + Level 1 + Level 2 (for Canfield waste edge cases).

---

### Step 1.8: Repeat for `undo_stock_to_all_tableau_move`

The stock_to_all_tableau undo is simpler:

**Pile undo:** Return each dealt card from its tableau pile back to stock, in reverse order.

**Descriptor undo:** Each card's descriptor goes back to STARTING (it was in the stock). This is always deterministic — no 1-byte store needed for this move type.

**sat_count:** Available from `move.count`. No undo stack needed.

**Validation:** Run unit tests + Level 1.

---

### Step 1.9: Remove the undo stack

After all components pass validation across Level 1 + Level 2:

1. **Delete `zobrist_undo_stack` entirely from `game_state.h`:**

```cpp
// DELETE all of this:
struct zobrist_undo { /* 10 bytes */ };
std::vector<zobrist_undo> zobrist_undo_stack;
```

No replacement data structure is needed. All undo values are computed from pile state.

2. **Simplify `make_regular_move`** — remove the pre-move captures and undo record push:

```cpp
void game_state::make_regular_move(const move m) {
    // Capture moved card identity before pile ops
    card moved = piles[m.from].top_card();
    uint8_t cid = zobrist_hash::card_id(moved.get_suit(), moved.get_rank());

    // Pile operations
    place_card(m.to, take_card(m.from));

    // Update moved card's descriptor
    uint8_t new_desc = determine_destination_descriptor(m.to, moved);
    update_card_descriptor(cid, new_desc);

    // Update foundation headers
    if (is_foundation_pile(m.from)) {
        uint8_t from_fs = get_foundation_suit(m.from);
        uint8_t new_rank = piles[m.from].empty()
            ? uint8_t(0) : piles[m.from].top_card().get_rank();
        update_foundation_in_hash(from_fs, new_rank);
    }
    if (is_foundation_pile(m.to)) {
        uint8_t to_fs = get_foundation_suit(m.to);
        update_foundation_in_hash(to_fs, moved.get_rank());
    }

    // Update hole header
    if (m.to == hole) {
        update_hole_top_in_hash(cid);
    }

    // Update waste pointer
    if (m.from == waste) {
        update_waste_ptr_in_hash(effective_waste_ptr());
    }

    // Handle reveal
    if (m.reveal_move) {
        assert(!piles[m.from].empty());
        assert(piles[m.from][0].is_face_down());
        piles[m.from][0].turn_face_up();
        card rev = piles[m.from][0];
        uint8_t rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());
        uint8_t rev_desc = (piles[m.from].size() == 1)
            ? compact_state::IN_SPACE
            : compact_state::STARTING_FACE_UP;
        update_card_descriptor(rev_cid, rev_desc);
    }

    // NO UNDO RECORD PUSHED — everything computed at undo time
}

void game_state::undo_regular_move(const move m) {
    // Pile operations FIRST
    place_card(m.from, take_card(m.to));

    // Identify moved card (now back at m.from)
    card moved = piles[m.from].top_card();
    uint8_t cid = zobrist_hash::card_id(moved.get_suit(), moved.get_rank());

    // Undo reveal (must happen before descriptor computation)
    if (m.reveal_move) {
        // Revealed card is at piles[m.from][1] after pile undo
        card rev = piles[m.from][1];
        uint8_t rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());
        update_card_descriptor(rev_cid, compact_state::STARTING);
        piles[m.from][1].turn_face_down();
    }

    // Undo foundation headers (read from restored pile state)
    if (is_foundation_pile(m.to)) {
        uint8_t to_fs = get_foundation_suit(m.to);
        uint8_t old_to_fr = piles[m.to].empty()
            ? uint8_t(0) : piles[m.to].top_card().get_rank();
        update_foundation_in_hash(to_fs, old_to_fr);
    }
    if (is_foundation_pile(m.from)) {
        uint8_t from_fs = get_foundation_suit(m.from);
        uint8_t old_from_fr = piles[m.from].top_card().get_rank();
        update_foundation_in_hash(from_fs, old_from_fr);
    }

    // Undo hole top (read from restored pile state)
    if (m.to == hole) {
        uint8_t old_ht = piles[hole].empty()
            ? uint8_t(0)
            : zobrist_hash::card_id(piles[hole].top_card().get_suit(),
                                     piles[hole].top_card().get_rank());
        update_hole_top_in_hash(old_ht);
    }

    // Undo waste pointer (read from restored pile state)
    if (m.from == waste) {
        update_waste_ptr_in_hash(effective_waste_ptr());
    }

    // Undo moved card descriptor (computed from pile state)
    uint8_t old_desc;
    if (piles[m.from].size() >= 2 && piles[m.from][1].is_face_down()) {
        // Face-down card below → card was revealed here → STARTING_FACE_UP
        old_desc = compact_state::STARTING_FACE_UP;
    } else {
        old_desc = determine_destination_descriptor(m.from, moved);
    }
    update_card_descriptor(cid, old_desc);

    // NO UNDO STACK — all values computed from restored pile state
}
```

3. **Remove the `#ifdef VALIDATE_INLINE_UNDO` blocks** and the CMake option.

4. **Remove the `zobrist_undo` struct and related declarations** from `game_state.h`.

**Validation:**
```bash
# Build WITHOUT validation flag (production mode)
./build.sh --release --unit-tests
cd cmake-build-release
ctest -R unit_tests --output-on-failure
ctest -R regression_level1 --output-on-failure
ctest -R regression_level2 --output-on-failure
```

**Exit criteria:** All unit tests pass. Level 1 + Level 2 regression pass. Level 3 regression pass (recommended but optional given Level 2 coverage).

---

## Summary: Everything Is Computed — Nothing Is Stored

| Component | Undo_move Computes From |
|---|---|
| Old card descriptor | Face-down check + `determine_destination_descriptor()` |
| Revealed card ID | `piles[m.from][1]` (or `[m.count]` for built groups) |
| Revealed card old desc | Always STARTING |
| Old foundation ranks | Pile tops after undo |
| Old hole top | Pile top after undo (0 if empty) |
| Old waste pointer | `effective_waste_ptr()` after undo |
| sat_count | `move.count` |

**Total per move: 0 bytes** (down from 10 bytes). The `zobrist_undo_stack` is eliminated entirely.

---

## Risk Mitigation

| Risk | Detection | Response |
|---|---|---|
| Face-down invariant violated for some game type | Step 1.5 assert comparing inline vs undo stack | Fall back to 1-byte stored descriptor for that game type |
| Revealed card at wrong pile index | Step 1.1 assert + comparison with `undo.revealed_card_id` | Fix index calculation |
| Foundation rank wrong after pile undo | Step 1.2 assert + comparison with `undo.old_from_found_rank` | Check pile undo ordering |
| Hole top wrong for empty hole | Step 1.3 assert on hole-game seeds (Level 2) | Verify empty sentinel value |
| Waste pointer wrong after stock_k_plus undo | Step 1.7 assert on Klondike/Canfield (Level 2) | Check pile undo sequence ordering |
| stock_to_all_tableau card count wrong | Step 1.8 assert | Verify `move.count` matches `undo.sat_count` |

---

## Ordering Constraints

Steps must be done in this order:

1. **Phase 0 complete** before any Phase 1 step
2. **Step 1.0** (scaffolding) before any component step
3. **Steps 1.1–1.5** for regular_move can be done in any order (each is independent)
4. **Step 1.6** (built_group) after Steps 1.1–1.5 are validated (reuses the same patterns)
5. **Step 1.7** (stock_k_plus) after Steps 1.1–1.5 are validated
6. **Step 1.8** (stock_to_all_tableau) can be done any time after Step 1.0
7. **Step 1.9** (remove undo stack) only after ALL previous steps pass Level 1 + Level 2

---

## Files Modified

| File | Phase 0 | Phase 1 |
|---|---|---|
| `src/main/game/search-state/game_state.cpp` | Add scaffolding | Rewrite undo functions |
| `src/main/game/search-state/game_state.h` | No change | Replace undo struct with 1-byte stack |
| `CMakeLists.txt` | Add `VALIDATE_INLINE_UNDO` option | Remove option after Step 1.9 |

No other files are modified. The cache implementations, solver, and test infrastructure are untouched.
