# Bug: ROOT Descriptor Overloaded — False Positives in Flat Cache

**Status:** FIXED — committed `52b8b63` (2026-03-26)
**Severity at discovery:** HIGH — false positives; solver skipped genuinely new states
**Branch:** `refactor-caching`
**Milestone:** M5 (Verification and Hardening)

---

## Summary

The flat cache produced false positive HITs — it reported a state as "already seen" when
it was a genuinely different board state. The `ROOT(2)` descriptor was overloaded: it
meant both "card at the bottom of a pile with nothing below it" and "card sitting on a
non-legal-build parent." Two different board configurations could therefore produce
identical payloads and hashes.

---

## Example (SpanishPatience seed 1, operations 8 and 9)

**Op 8:** 9D is at the bottom of column 3 with 4S and QH stacked above it (not in legal
build order). Column 0 is empty.
**Op 9:** 9D has been moved to the bottom of column 0. 4S is now the bottom card of
column 3.

These are genuinely different states — different cards, different piles, different
available moves. But with the original `ROOT` descriptor:

- Both 9D at a pile bottom and 4S at a pile bottom got `ROOT(2)`
- 4S sitting on non-legal-parent 9D also got `ROOT(2)` as a fallback
- All descriptors were identical across the two states
- Payload bytes 3–31 were identical; Zobrist hash was identical
- `flat_cache::insert()` returned HIT (false positive); `lru_cache` correctly returned MISS

---

## Affected Game Types

Most likely to affect games where the deal places cards in non-legal-build order on the
tableau:

- SpanishPatience (13 piles, ANY_SUIT build — non-adjacent ranks dealt together)
- FlowerGarden (similar non-legal stacking from deal)
- SeahavenTowers (similar pattern)
- Any game where `get_descriptor_for_parent()` falls back to ROOT for starting positions

---

## Fix: IN_SPACE(9) Descriptor

A new descriptor value was introduced and ROOT was redefined:

| Value | Name | Meaning |
|---|---|---|
| 2 | ROOT | Card sitting on a non-legal-build parent (fallback only) |
| 9 | IN_SPACE | Card at the bottom of a tableau pile (empty space below, or only face-down cards below) |

**Assignment rules:**
- `IN_SPACE` at init: bottom-of-pile face-up cards in `init_payload_and_hash()`
- `IN_SPACE` on move: `determine_destination_descriptor()` when placing on empty pile
- `IN_SPACE` on reveal: when a face-down card is revealed AND it is the pile's bottom card
- `ROOT` at init: cards on non-legal-build parents
- `ROOT` on move: `determine_destination_descriptor()` fallback when parent not in build table

**Why this fixes the SpanishPatience case:**
- Op 8: 4S on non-legal-parent 9D → `ROOT(2)`. 9D at pile bottom → `IN_SPACE(9)`.
- Op 9: 4S at pile bottom → `IN_SPACE(9)`. 9D at pile bottom → `IN_SPACE(9)`.
- 4S descriptor changed (ROOT→IN_SPACE) → different payload → no false positive.

**Nuance for revealed cards:** Only a card at the very bottom of the pile (no cards
below it, face-down or face-up) gets `IN_SPACE` on reveal. A revealed card with face-down
cards still below it gets `STARTING_FACE_UP(1)` instead.

---

## Test Results After Fix

All 9 DualCacheTest agreement tests pass with zero mismatches in either direction:
FreeCell, BakersGame, EightOff, SpanishPatience, Somerset, FlowerGarden,
FortunesFavor, SeahavenTowers, Klondike.

---

## Relationship to the STARTING Descriptor Bug

Both bugs follow the same underlying principle: the descriptor assigned at
initialisation must be the same as the descriptor that `determine_destination_descriptor()`
would compute for that position. The STARTING bug violated this for `STARTING(0)` vs
`PARENT_x`; the ROOT bug violated it for `ROOT(2)` vs the newly introduced `IN_SPACE(9)`.
The shared fix: make `init_payload_and_hash()` canonical with respect to move descriptors.
