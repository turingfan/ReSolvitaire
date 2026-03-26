# Bug Report: ROOT Descriptor Causes False Positives in Flat Cache

## Summary

The flat cache produced **false positive HITs** — it reported a state as "already
seen" when it was genuinely a different board state. The original ROOT descriptor
did not distinguish between different reasons a card could be at the bottom of a
tableau pile.

**Status**: FIXED (2026-03-26). Two new descriptor semantics resolve the issue.

**Severity**: HIGH — false positives cause the solver to skip states it should
explore, potentially missing solutions or reporting UNSOLVABLE incorrectly.

---

## Evidence

### SpanishPatience Seed 1 — Ops 8 and 9

At op 8 the board has 9D at the bottom of column 3, with 4S and QH stacked
above it (not in legal build order under ANY_SUIT policy). Column 0 is empty.
At op 9, 9D has been moved to the bottom of column 0, and 4S is now the bottom
card of column 3.

These are **genuinely different board states** — different cards in different
piles with different available moves. However, with the original ROOT descriptor:

- Both 9D and 4S at their respective pile bottoms got ROOT(2)
- 4S sitting on non-legal-parent 9D also got ROOT(2) as a fallback
- All descriptors were identical between the two states
- Payload bytes 3-31 were identical; Zobrist hash was identical
- flat_cache::insert() returned HIT (false positive)
- lru_cache::insert() correctly returned MISS

### Affected Games

Most likely to affect games where cards are stacked in non-legal-build order:
- SpanishPatience (13 piles, ANY_SUIT build, random deal stacks non-adjacent ranks)
- FlowerGarden (similar non-legal stacking from deal)
- SeahavenTowers (similar pattern)
- Any game where the deal places cards on non-matching parents

---

## Root Cause

The original descriptor model used ROOT(2) for three distinct situations:
1. A card at the bottom of a pile in its starting position
2. A card moved to an empty pile
3. A card sitting on a non-legal-build parent (fallback when `get_descriptor_for_parent` returns 0)

Cases 1/2 and case 3 are fundamentally different: case 3 means the card has a
card below it (just not a legal build parent), while cases 1/2 mean the card has
nothing below it (or only face-down cards). Conflating them loses information
about the pile structure.

---

## Fix: IN_SPACE(9) Descriptor and ROOT Redefinition

Added a new descriptor value and redefined ROOT:

| Value | Name | Meaning |
|---|---|---|
| 2 | ROOT | Card sitting on a non-legal-build parent (fallback) |
| 9 | IN_SPACE | Card at the bottom of a tableau pile (empty space below) |

### Where each is assigned:

- **IN_SPACE at init**: Bottom-of-pile face-up cards in `init_payload_and_hash()`
- **IN_SPACE on move**: `determine_destination_descriptor()` when placing on empty pile
- **IN_SPACE on reveal**: When a face-down card is revealed AND it is the bottom card of the pile
- **ROOT at init**: Cards sitting on non-legal-build parents (fallback in `init_payload_and_hash()`)
- **ROOT on move**: `determine_destination_descriptor()` fallback when parent not in build table
- **ROOT in built-group**: Same fallback in `make_built_group_move()`

### Why this fixes the SpanishPatience false positive:

- Op 8: 4S on non-legal-parent 9D → **ROOT(2)**. 9D at bottom → **IN_SPACE(9)**.
- Op 9: 4S at bottom of pile → **IN_SPACE(9)**. 9D at bottom → **IN_SPACE(9)**.
- 4S descriptor changed (ROOT→IN_SPACE) → different payload → no false positive.

### Consistency with STARTING bug fix:

The IN_SPACE descriptor at init matches what `determine_destination_descriptor()`
returns for the same position. A card moved away from the bottom of a pile and
placed back at the bottom of any pile gets IN_SPACE both times. This avoids
the STARTING-style false negatives where init and move descriptors diverged.

---

## Test Results After Fix

All 9 DualCacheTest agreement tests pass with zero mismatches in either direction:
FreeCell, BakersGame, EightOff, SpanishPatience, Somerset, FlowerGarden,
FortunesFavor, SeahavenTowers, Klondike.

---

## Relationship to Other Bugs

- **STARTING descriptor bug** (fixed same session): STARTING(0) was not
  position-canonical. Fix: compute positional descriptors at init. The IN_SPACE
  fix follows the same principle — init descriptors must match move descriptors.

- **Waste pointer symmetry** (fixed same session): `effective_waste_ptr()`
  mirrors LRU's `waste_deal_symmetry` condition.
