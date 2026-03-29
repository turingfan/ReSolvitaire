# Bug: STARTING Descriptor Not Position-Canonical

**Status:** FIXED — committed `52b8b63` (2026-03-26)
**Severity at discovery:** HIGH — false negatives; flat cache missed states already seen
**Branch:** `refactor-caching`
**Milestone:** M5 (Verification and Hardening)

---

## Summary

The flat cache produced false negatives — it failed to recognise states it had already
seen, causing the solver to re-explore portions of the game tree. The root cause was that
`init_payload_and_hash()` assigned `STARTING(0)` to all face-up tableau cards regardless
of their actual position, but `determine_destination_descriptor()` computed position-based
descriptors (`ROOT`, `PARENT_0–3`) when a card was placed. When a card was moved away and
returned to the same position via a different path, the two descriptors diverged: the LRU
cache (which reconstructs its representation from scratch) saw the same state; the flat
cache (which maintains descriptors incrementally) did not.

---

## Example (FreeCell seed 1, operation 7)

Card 8C starts on 9D. Via path A it never moves: descriptor stays `STARTING(0)`. Via
path B it is moved away and returned to 9D: descriptor becomes `PARENT_1(5)` from
`determine_destination_descriptor`. Same board, different payload, different hash —
flat cache misses the hit that LRU correctly finds.

---

## Evidence

- 5 dual_cache unit tests failed with `LRU=HIT, Flat=MISS` pre-eviction:
  FreeCell seed 1 (op 7), BakersGame seed 1 (op 211), Somerset seed 3 (op 191),
  FlowerGarden seed 1 (op 99), SeahavenTowers seed 1 (op 8853).
- All failures followed the same pattern: LRU hit, flat miss, no hash collision.

---

## Fix

`init_payload_and_hash()` now computes the correct positional descriptor for every
face-up tableau card — matching what `determine_destination_descriptor()` would compute
for the same position:

- Bottom-of-pile (empty space below): `IN_SPACE(9)` (see also related bug below)
- On a non-legal-build parent: `ROOT(2)`
- On a legal-build parent: `PARENT_0–3(4–7)`

`STARTING(0)` is now reserved exclusively for face-down cards.

---

## Related Bugs Fixed in the Same Session

- **ROOT descriptor overloaded** — `ROOT` was used for both "bottom of pile" and
  "on non-legal-build parent", causing false positives. Fix: introduce `IN_SPACE(9)`.
  See `bug_root_descriptor_false_positives.md`.
- **Waste pointer stale on regular moves** — `make_regular_move()` did not update the
  waste pointer when the source pile was `waste`. See `bug_level2_regression_mismatches.md`.

---

## Design Note: Implications for Future Rule Variants

If a future game variant made a card's history meaningful (e.g., "a card that has never
been moved has special properties"), the descriptor model would need to distinguish
"originally placed here" from "moved back here." This would require tracking original
positions or a separate mechanism outside the descriptor model. No current game variant
has this property.
