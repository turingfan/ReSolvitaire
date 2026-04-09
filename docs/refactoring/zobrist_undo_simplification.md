# Zobrist Undo Simplification: A Pile-First Approach

**Date:** 2026-04-09
**Author:** Ian Gent + Claude
**Status:** Proposal (not yet reviewed)

---

## Core Insight

Undoing a move is **almost exactly like making a move in reverse order**. The key is: do pile operations first, then read from the restored pile state to compute what the old values were.

**Current approach:** Capture old values → pile ops → update hash → store undo record
**Proposed approach:** Pile ops (reversed) → read restored state → compute old values → update hash

This eliminates the need for `zobrist_undo_stack` entirely. The old values are **computed on-the-fly**, not stored.

---

## The Pattern

### Current (Stack-Based) Flow

```
make_regular_move(m):
  1. old_desc = payload.get_descriptor(cid)          // CAPTURE OLD
  2. place_card(m.to, take_card(m.from))             // PILE OPS
  3. new_desc = determine_destination_descriptor(...)
     update_card_descriptor(cid, new_desc)           // UPDATE HASH
  4. (handle reveals, foundations, etc.)
  5. zobrist_undo_stack.push_back({old_desc, ...})   // STORE

undo_regular_move(m):
  1. undo = zobrist_undo_stack.pop()                 // READ STACK
  2. (undo reveal descriptor using undo.revealed_card_id)
  3. update_card_descriptor(cid, undo.old_desc)      // RESTORE USING STACK
  4. place_card(m.from, take_card(m.to))             // PILE OPS
```

**Problem:** Stack memory overhead + undo values stored separately from code logic.

---

### Proposed (Computed-On-the-Fly) Flow

```
undo_regular_move(m):
  1. place_card(m.from, take_card(m.to))             // PILE OPS FIRST
     // Now the card is back at m.from, piles restored to pre-move state

  2. card moved = piles[m.from].top_card()
     new_desc = payload.get_descriptor(cid)          // What was SET during make_move
     old_desc = determine_destination_descriptor(m.from, moved)  // Compute what IT SHOULD BE

  3. (undo reveal by reading piles[m.from][1])       // COMPUTE FROM PILES

  4. update_card_descriptor(cid, old_desc)           // RESTORE using COMPUTED value
     (undo foundations, hole, waste using restored piles)
```

**Advantage:** Values computed from restored pile state. No undo stack. Logic is local and clear.

---

## Detailed Example: make/undo_regular_move

### Current Code (Stack-Based)

```cpp
void game_state::make_regular_move(const move m) {
    // Step 1: Capture old state
    card moved = piles[m.from].top_card();
    uint8_t cid = zobrist_hash::card_id(moved.get_suit(), moved.get_rank());
    uint8_t old_desc = payload.get_descriptor(cid);  // CAPTURE

    uint8_t from_fs = 255, old_from_fr = 255;
    if (is_foundation_pile(m.from)) {
        from_fs = get_foundation_suit(m.from);
        old_from_fr = payload.get_foundation(from_fs);  // CAPTURE
    }
    // ... capture to_fs, old_to_fr, old_ht, old_waste_ptr

    // Step 2: Pile operations
    place_card(m.to, take_card(m.from));

    // Step 3: Compute and update new state
    uint8_t new_desc = determine_destination_descriptor(m.to, moved);
    update_card_descriptor(cid, new_desc);

    if (from_fs != 255) {
        uint8_t new_rank = piles[m.from].empty()
            ? uint8_t(0) : piles[m.from].top_card().get_rank();
        update_foundation_in_hash(from_fs, new_rank);
    }
    // ... update other headers

    // Step 5: Store undo record with captured values
    zobrist_undo undo = {cid, old_desc, rev_cid, from_fs, old_from_fr, ...};
    zobrist_undo_stack.push_back(undo);
}

void game_state::undo_regular_move(const move m) {
    // Pop the undo record (stores the old values we captured)
    zobrist_undo undo = zobrist_undo_stack.back();
    zobrist_undo_stack.pop_back();

    // Undo reveal
    if (m.reveal_move) {
        update_card_descriptor(undo.revealed_card_id, compact_state::STARTING);
        piles[m.from][0].turn_face_down();
    }

    // Restore using captured old values
    update_hole_top_in_hash(undo.old_hole_top);
    update_waste_ptr_in_hash(undo.old_waste_ptr);
    update_foundation_in_hash(undo.to_found_suit, undo.old_to_found_rank);
    update_foundation_in_hash(undo.from_found_suit, undo.old_from_found_rank);
    update_card_descriptor(undo.card_id, undo.old_desc);  // Use CAPTURED value

    // Pile operations (now at the end)
    place_card(m.from, take_card(m.to));
}
```

---

### Proposed Code (Computed-On-the-Fly)

```cpp
void game_state::undo_regular_move(const move m) {
    // Step 1: Pile operations FIRST (reverse)
    place_card(m.from, take_card(m.to));

    // Now piles are restored to pre-move state. The card is back at m.from.
    // But payload descriptors are still in the "post-move" state.

    // Step 2: Extract the moved card (now back at m.from)
    card moved = piles[m.from].top_card();
    uint8_t cid = zobrist_hash::card_id(moved.get_suit(), moved.get_rank());

    // Step 3: Determine what the descriptor should be NOW that card is back at m.from
    //         This is exactly what it was BEFORE the make_move call.
    uint8_t new_desc = payload.get_descriptor(cid);     // What make_move set
    uint8_t old_desc = determine_destination_descriptor(m.from, moved);  // Compute

    // Step 4: Undo reveal BEFORE changing descriptor
    if (m.reveal_move) {
        // After pile undo, the revealed card is now at piles[m.from][1] (below returned card)
        assert(!piles[m.from][1].is_face_down());
        uint8_t rev_cid = zobrist_hash::card_id(
            piles[m.from][1].get_suit(),
            piles[m.from][1].get_rank());

        // Determine reveal descriptor based on restored pile state
        uint8_t rev_desc_after_reveal = (piles[m.from].size() == 1)
            ? compact_state::IN_SPACE
            : compact_state::STARTING_FACE_UP;

        // Update back to pre-reveal state
        update_card_descriptor(rev_cid, compact_state::STARTING);
        piles[m.from][1].turn_face_down();
    }

    // Step 5: Undo foundation headers (reading from restored piles)
    if (is_foundation_pile(m.from)) {
        uint8_t from_fs = get_foundation_suit(m.from);
        uint8_t old_from_fr = piles[m.from].empty()
            ? uint8_t(0) : piles[m.from].top_card().get_rank();
        update_foundation_in_hash(from_fs, old_from_fr);
    }

    if (is_foundation_pile(m.to)) {
        uint8_t to_fs = get_foundation_suit(m.to);
        // After pile undo, m.to has the card removed, so second-from-top is old top
        uint8_t old_to_fr = (piles[m.to].size() >= 2)
            ? piles[m.to][1].get_rank()
            : uint8_t(0);
        update_foundation_in_hash(to_fs, old_to_fr);
    }

    // Step 6: Undo hole header (reading from restored piles)
    if (m.to == hole && !piles[hole].empty()) {
        // After pile undo, hole has the card removed, so top is the old hole_top
        uint8_t old_ht_cid = zobrist_hash::card_id(
            piles[hole].top_card().get_suit(),
            piles[hole].top_card().get_rank());
        update_hole_top_in_hash(old_ht_cid);
    } else if (m.to == hole && piles[hole].empty()) {
        // Hole is now empty; update to invalid value (255)
        // Note: payload.hole_top is currently set to moved card from make_move
        // We need to XOR it out. Calling with invalid card_id reverts to empty sentinel.
        update_hole_top_in_hash(255);  // or use a special "no card" marker
    }

    // Step 7: Undo waste pointer (reading from restored piles)
    if (m.from == waste) {
        // After pile undo, waste pile is restored to pre-move state
        // effective_waste_ptr() reads the current waste state and applies symmetry
        update_waste_ptr_in_hash(effective_waste_ptr());
    }

    // Step 8: Undo moved card descriptor
    update_card_descriptor(cid, old_desc);

    // NO UNDO STACK NEEDED. All values computed from restored pile state.
}
```

---

## Why This Works: The Restoration Principle

After reversing the pile operations in undo, the piles are in **exactly the same state as before make_move was called**. Therefore:

1. **`determine_destination_descriptor(m.from, moved)`** computes the descriptor for a card at position m.from, which is **exactly what old_desc was** before the move.

2. **`piles[m.from].top_card().get_rank()`** reads the top card of the source pile (if it was a foundation), which **is the old foundation rank** from before take_card removed the moved card.

3. **`piles[m.to][1].get_rank()`** reads the second-from-top card of the destination (if it was a foundation), which **is the old rank before the moved card was placed**.

4. **`effective_waste_ptr()`** computes the waste pointer based on current waste pile size, which **is the old waste_ptr** after the pile state is restored.

5. **`piles[hole].top_card()`** (after pile undo) reads what's now at the top of the hole pile, which **is the old hole_top** before the moved card was placed.

6. **`piles[m.from][1]` (when reveal_move is set)** is the revealed card after pile undo (it's below the returned card), and we can **read its card_id directly**.

**The key insight:** The pile state IS the source of truth. After undoing pile ops, it's restored. Reading from it gives us the old values.

---

## Move Type Coverage

This approach generalizes to all move types:

### 1. make/undo_regular_move ✓
- Moved card descriptor: ✓ (determine_destination_descriptor after pile undo)
- Revealed card: ✓ (read piles[m.from][1])
- Foundation ranks: ✓ (read pile state)
- Hole top: ✓ (read pile[hole][0])
- Waste pointer: ✓ (call effective_waste_ptr())

### 2. make/undo_built_group_move ✓
- Moved card descriptor (bottom card): ✓ (determine_destination_descriptor computes based on parent at m.to[m.count])
- Revealed card: ✓ (read piles[m.from][1])

### 3. make/undo_stock_k_plus_move ✓
- Played card descriptor: ✓ (determine_destination_descriptor(m.to, ...))
- Foundation rank (if to_fs): ✓ (read pile state)
- Hole top (if m.to == hole): ✓ (read pile[hole][0])
- Waste pointer: ✓ (call effective_waste_ptr() after stock/waste redealing)
- Old waste pointer before dealing: ✓ (call effective_waste_ptr() before any dealings occur... **see below**)

### 4. make/undo_stock_to_all_tableau ✓
- Multiple descriptor updates: Each card placed from stock to tableau is read from m.from (the stock card); after pile undo, all cards are back in stock, and we can recompute their descriptors as STARTING (or whatever they were as foundation top cards).
- sat_count: Encoded in move.count, not needed in undo record.

### 5. make/undo_sequence_move ✓
- Similar to built_group; the bottom card of the sequence is what changes descriptor.

### 6. make/undo_accordion_move ✓
- Predecessor updates: Can be undone by reading current predecessor array and XORing with the Z_pred table values.

---

## Subtle Cases to Handle

### Case 1: Waste Pointer in stock_k_plus_move

In `make_stock_k_plus_move`, the waste pointer is updated **twice**:

```cpp
// Step 1: Deal cards from stock to waste
for (int i = 0; i < m.count; i++) {
    place_card(waste, take_card(stock));
}

// Step 2: Play top card from waste to destination
place_card(m.to, take_card(waste));

// Step 3: Flip waste back to stock (if redeal)
if (m.flip_waste) {
    while (!piles[waste].empty()) {
        place_card(stock, take_card(waste));
    }
}

// Update waste pointer (what it is NOW, after all the dealing)
update_waste_ptr_in_hash(effective_waste_ptr());
```

The undo record captures `old_waste_ptr` **before step 1**. When undoing, we need to restore to that state.

**Solution:** In undo, **reverse all the pile operations in correct order**, then call `effective_waste_ptr()`:

```cpp
// Undo in EXACT REVERSE ORDER of make_move:

// Step 1: Undo flip_waste (if applicable)
if (m.flip_waste) {
    while (!piles[stock].empty()) {
        place_card(waste, take_card(stock));
    }
}

// Step 2: Undo play to destination
place_card(waste, take_card(m.to));

// Step 3: Undo deal from stock
for (int i = 0; i < m.count; i++) {
    place_card(stock, take_card(waste));
}

// NOW effective_waste_ptr() returns what it was BEFORE the move
update_waste_ptr_in_hash(effective_waste_ptr());
```

The waste pile is now restored to its pre-move state, so `effective_waste_ptr()` returns the old value. **No undo stack needed.**

### Case 2: Hole Top When Destination is Empty Hole

If a card is placed in an empty hole (m.to == hole and hole was empty):

```cpp
// make_move:
if (m.to == hole) {
    old_ht = payload.get_hole_top();  // This is some "empty" marker or prior card
    update_hole_top_in_hash(cid);     // Set hole to moved card
}

// undo_move:
if (m.to == hole) {
    // After pile undo, hole is empty again (card removed)
    // How do we know what old_ht was?
    if (piles[hole].empty()) {
        // Need a marker for "empty hole"
        update_hole_top_in_hash(255);  // or some invalid card_id
    } else {
        uint8_t old_ht_cid = zobrist_hash::card_id(piles[hole][0]...);
        update_hole_top_in_hash(old_ht_cid);
    }
}
```

**Question:** What is the "empty hole" marker in zobrist hashing? Look at how `update_hole_top_in_hash()` handles 255 or invalid cards. If 255 is already the convention, use it. If hole starts as "255" (no card), the initial hash includes Z_hole_top[255]. To revert to empty, XOR out the moved card's key and XOR in 255's key... but if 255 is "no card", does it even have a key?

**Check:** Look at `zobrist.h` to see if `Z_hole_top[255]` exists or is defined specially. If hole_top is always a valid 0-51 card ID, then we need a convention for "empty" (perhaps card 52 or 255 with special handling).

---

### Case 3: Revealed Card When Previous Pile was Empty

In the reveal logic, if the revealed card is the only card left in the pile:

```cpp
// make_move:
uint8_t rev_desc = (piles[m.from].size() == 1)
    ? compact_state::IN_SPACE
    : compact_state::STARTING_FACE_UP;

// undo_move (after pile undo):
if (m.reveal_move) {
    // piles[m.from] now has [returned_card, revealed_card]
    uint8_t rev_desc = (piles[m.from].size() == 1)  // FALSE, it's now size 2
        ? compact_state::IN_SPACE
        : compact_state::STARTING_FACE_UP;
    // This is WRONG! We set it to STARTING_FACE_UP, but it should be IN_SPACE
}
```

**Problem:** After pile undo, the returned card is on top, so size is different. We can't use size to determine the old descriptor.

**Solution:** Store the **old reveal descriptor** in the undo record, or...

**Better solution:** The revealed card's descriptor is **always** `STARTING` before the reveal. We're reverting to face-down in the same position. So:

```cpp
update_card_descriptor(rev_cid, compact_state::STARTING);
```

The `STARTING` descriptor covers both cases (IN_SPACE and STARTING_FACE_UP). We just need to revert to "the card is face-down and hasn't moved yet". That's `STARTING` for any card.

---

## Data Structure Changes

### Removing from game_state.h

Delete:
```cpp
struct zobrist_undo {
    uint8_t card_id;
    uint8_t old_desc;
    uint8_t revealed_card_id;
    uint8_t from_found_suit;
    uint8_t old_from_found_rank;
    uint8_t to_found_suit;
    uint8_t old_to_found_rank;
    uint8_t old_hole_top;
    uint8_t old_waste_ptr;
    uint8_t sat_count;
};
std::vector<zobrist_undo> zobrist_undo_stack;
```

Also delete:
```cpp
struct predecessor_undo {
    uint8_t card_id;
    uint8_t old_pred;
};
struct predecessor_undo_frame {
    uint8_t count;
};
std::vector<predecessor_undo> pred_undo_entries;
std::vector<predecessor_undo_frame> pred_undo_frames;
```

### Keeping in game_state.h

Keep all existing methods and fields for pile state, payload, hashing, etc. The only change is how `undo_*_move` functions work internally.

---

## Validation Strategy

### 1. Correctness (Dual-Tracking Asserts)

Implement the new undo logic in parallel with the old stack-based logic:

```cpp
void game_state::undo_regular_move(const move m) {
    // OLD PATH: Pop undo stack, use old values
    zobrist_undo undo = zobrist_undo_stack.back();
    zobrist_undo_stack.pop_back();
    uint64_t hash_before_old = zobrist_hash_value;
    // ... undo using undo record ...
    uint64_t hash_after_old = zobrist_hash_value;

    // NEW PATH: Pile undo first, compute old values
    // Restore to before-old state
    zobrist_hash_value = hash_before_old;
    // (reverse the undo operations to restore)
    uint64_t hash_after_new = zobrist_hash_value;

    // ... undo using computed values ...
    // Final hash_after_new should match hash_after_old

    // ASSERT: Both paths produce identical hash
    assert(hash_after_old == hash_after_new);
}
```

Run all 5 regression levels with these dual-tracking asserts enabled.

### 2. Memory Savings Verification

Measure memory usage before and after:
```bash
# Before (with undo stack)
/usr/bin/time -v ./solvitaire --type klondike --random 42

# After (without undo stack)
/usr/bin/time -v ./solvitaire --type klondike --random 42
```

Expected: ~10-15% reduction in peak memory (undo_stack was large for deep searches).

### 3. Performance Benchmark

The new approach may be slightly slower (computing old values instead of reading them), but should be negligible compared to the memory savings. Benchmark on 50-seed klondike test set:

```bash
./solvitaire --type klondike --random <seed> | grep states_searched
```

Expected: ~same number of states searched (hash correctness preserved).

---

## Risk Mitigation

### Highest Risk: Revealing Cards in Empty Piles

The descriptor for a revealed card depends on pile size at reveal time. If the pile becomes empty after the reveal, we lose information.

**Mitigation:** Store the revealed card's old descriptor in the undo record, OR always use `STARTING` as the revert descriptor (since reveal just turns face-up; undoing turns face-down to `STARTING`).

**Recommendation:** Use `STARTING` unconditionally for reveals. This is correct because:
- Before reveal, card is face-down at some position
- Reveal turns it face-up but doesn't move it
- Undo restores face-down, which is `STARTING` regardless of position

### Medium Risk: Foundation Ranks After Card Placed

If a foundation was empty before the move, and a card is placed on it, `old_rank = 0`. After pile undo, the foundation is empty again.

**Mitigation:** Check `if (piles[m.from/m.to].empty()) then 0 else top_card().rank()`. This is already how the code works.

### Medium Risk: Hole Top When Hole is Empty

Same as foundation case. If hole was empty, `old_ht = 255` (sentinel). After pile undo, hole is empty again.

**Mitigation:** Check pile state and use sentinel value (255) if empty.

### Low Risk: Waste Pointer Symmetry

The waste pointer uses `effective_waste_ptr()` which applies symmetry rules. As long as the waste pile is restored correctly, this should work.

**Mitigation:** Test on waste-dealing games explicitly (Klondike, Double Klondike).

---

## Implementation Roadmap

1. **Code Review:** Have someone review the logic for each move type before implementation.
2. **Implement undo_regular_move** with dual-tracking asserts.
3. **Run unit tests** to ensure no crashes.
4. **Run Level 1 regression** with asserts enabled.
5. **Remove undo stack** from game_state.h.
6. **Run Level 2 regression** (catches Canfield, hole games edge cases).
7. **Measure memory savings** and performance.
8. **Run Level 3 regression** for confidence.

---

## Estimated Scope

- **Lines changed:** ~500-700 (6 undo_*_move functions rewritten)
- **Lines deleted:** ~50-100 (undo struct definitions)
- **New code:** ~200-300 (descriptor/value reconstruction logic)
- **Test code added:** ~100 (dual-tracking asserts)

Much simpler than initially thought!

---

## Conclusion

The insight that "undo is make in reverse" leads to a much simpler implementation:
- **No undo stack needed** (memory savings ~10-15%)
- **Values computed from restored pile state** (clear, local logic)
- **Same hash correctness** (validation via dual-tracking asserts)
- **Generalizes to all move types** (pattern repeats)

The implementation is straightforward and lower-risk than the "reconstruct from move object" approach outlined in the earlier execution strategy. This is the path forward.
