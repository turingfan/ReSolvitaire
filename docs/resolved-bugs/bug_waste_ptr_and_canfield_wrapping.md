# Bug: Waste Pointer Stale on Regular Moves; Canfield Wrapping Builds Unrecognised

**Status:** FIXED — committed `52b8b63` (2026-03-28)
**Severity at discovery:** Medium (waste pointer) / High (canfield false positives)
**Branch:** `refactor-caching`
**Milestone:** M5 (Verification and Hardening)

These two bugs were discovered together via Level 2 regression mismatches and the
dual-cache mismatch diagnostic, and fixed in the same commit.

---

## Bug A: Waste Pointer Stale on Regular Moves (FortunesFavor)

### Summary

`make_regular_move()` never called `update_waste_ptr_in_hash()` when the source pile was
`waste`. Games using `auto-waste-then-stock` spaces policy auto-play waste cards to empty
tableau spaces via regular moves (not stock moves). The waste pile shrank, but the
payload's waste pointer (byte 5) retained the old value. This caused false negatives:
the flat cache computed a different hash for the same board state reached via different
paths.

### Evidence

FortunesFavor seed 31646033: 18,908 `LRU=HIT, Flat=MISS` mismatches. At ops 13 and 22
the board was identical; descriptors were identical; only payload byte 5 (waste_ptr)
differed (`0x12` vs `0x13`). Node count increased 73% (198,928 vs oracle 114,880). Outcome
(unsolvable) was still correct.

### Fix

In `make_regular_move()` (`game_state.cpp`):
1. Save old waste pointer before the move: `uint8_t old_wp = effective_waste_ptr()`
2. Call `update_waste_ptr_in_hash(effective_waste_ptr())` after moving the card
3. Store `old_wp` in the `zobrist_undo` record for restoration in `undo_regular_move()`

---

## Bug B: Canfield Wrapping Builds Not Recognised (CanfieldStrict)

### Summary

`parent_table::get_parents()` used a hard-coded `rank + 1` formula and returned no
parents for Kings. Canfield variants use a non-Ace foundation base (e.g., base = King),
creating a wrapping build sequence: King→Ace→2→…→Queen. Without wrapping, King was
treated as having no legal parents, so cards built on a King (legally) received `ROOT(2)`
as a fallback instead of `PARENT_x`. This caused false positives: two states where
different cards held the ROOT/PARENT distinction were encoded identically.

### Evidence

CanfieldStrict seed 4000100: 87 `LRU=MISS, Flat=HIT` divergences. Ops 418 and 673 had
matching payloads despite genuinely different board states (KS in different piles; AD and
AH swapping between ROOT and IN_SPACE). The diagnostic initially pointed to a reserve
encoding issue; the actual cause was KS having no recognised parents in the build table
because Kings were excluded by the hard-coded formula.

### Fix

`parent_table::get_parents()` now accepts `foundations_base` and `max_rank` parameters.
It applies `foundation_base_convert` logic to compute the correct parent rank, handling
the wrapping case:

```
parent_rank = ((child_rank - foundations_base) % max_rank + foundations_base)
```

For a King (rank 13) with `foundations_base = 13` (King-base game): parent rank becomes
Queen (12), which is a legal build parent. Four call sites in `game_state.cpp` updated;
five unit tests added to `zobrist_test.cpp`.

---

## Related Context

Both bugs were identified via the dual-cache mismatch diagnostic infrastructure built
in M5 (`mismatch_diagnostic.cpp`). The diagnostic runs the solver with a `dual_cache`
(flat + LRU side by side), records every cache operation, and reports the first point
of divergence with a per-card descriptor diff. Without this infrastructure, both bugs
would have been very difficult to locate.
