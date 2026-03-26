# Bug Report: Dual Cache Mismatches

## Summary

5 dual_cache unit tests fail with `LRU=HIT, Flat=MISS` mismatches occurring pre-eviction. In all cases, the lru_cache finds and recognizes states that the flat_cache misses, causing different behavior during search.

**Status**: ROOT CAUSE IDENTIFIED AND FIXED (2026-03-26). See "Root Cause" section below.

---

## Detailed Findings

### FreeCell — Seed 1

- **First mismatch**: Operation 7
- **LRU behavior**: HIT (state already in cache)
- **Flat behavior**: MISS (state not found in cache)
- **Zobrist hash**: `0xdb8f8f7f773a789d`
- **Assessment**: Extremely early mismatch (op 7) suggests encoding or initialization issue

### BakersGame — Seed 1

- **First mismatch**: Operation 211
- **LRU behavior**: HIT
- **Flat behavior**: MISS
- **Zobrist hash**: `0xe422c6c64f9ce6b0`
- **Assessment**: Later in search; consistent with FreeCell pattern

### Somerset — Seed 1

- **Status**: No mismatches detected at seed 1

### Somerset — Seed 3

- **First mismatch**: Operation 191
- **LRU behavior**: HIT
- **Flat behavior**: MISS
- **Zobrist hash**: `0xe4e4f15ee467ebf2`
- **Assessment**: Seed-dependent; not occurring in seed 1

### FlowerGarden — Seed 1

- **First mismatch**: Operation 99
- **LRU behavior**: HIT
- **Flat behavior**: MISS
- **Zobrist hash**: `0xf34c593807a3a2c4`
- **Assessment**: Occurs relatively early; consistent pattern

### SeahavenTowers — Seed 1

- **First mismatch**: Operation 8853
- **LRU behavior**: HIT
- **Flat behavior**: MISS
- **Zobrist hash**: `0xd4cdb68ad5c715e5`
- **Assessment**: Latest in search sequence; same pattern as others

---

## Pattern Analysis

### Common Pattern

All failures follow the same pattern:
- **LRU cache hits** on a state
- **Flat cache misses** the same state
- Occurs **pre-eviction** (before cache replacement pressure)
- **Zobrist hashes are present** and unique per mismatch

### Not Observed

- No `LRU=MISS, Flat=HIT` (flat never over-prunes)
- No eviction-based mismatches
- No hash collisions apparent

---

## Possible Causes (Speculation Only)

The fact that lru_cache finds states flat_cache doesn't suggests one of:

1. **Descriptor encoding mismatch**: Different descriptor values for equivalent card states
2. **State payload divergence**: Incremental updates to flat_cache payload not matching game state reality
3. **Hash computation drift**: Zobrist hash computed differently between caches despite same payload
4. **Pile canonicalization**: LRU's pile sorting creates equivalence classes flat_cache doesn't recognize
5. **Initialization issue**: First few operations diverge due to setup differences

---

## Evidence Needed

To debug further, need to:

1. Dump the full game state at each mismatch point
2. Compare `game_state` with `compact_state` payload for accuracy
3. Trace descriptor assignment for cards in the mismatch state
4. Compare zobrist hash computation between incremental and from-scratch
5. Verify pile ordering matches between the two encodings

---

## Unit Test Impact

- **Total tests**: 196
- **Passing**: 191
- **Failing**: 5 (these mismatches)
- **Outcome tests**: All passing (correct solutions despite different node counts)

## Deep Dive: FreeCell Op 7

**Key Finding**: When FreeCell is solved deterministically (always taking first move), both caches correctly report MISS for op 7 — the state is genuinely new. However, during DFS with backtracking, LRU reports HIT while flat reports MISS.

**Analysis**:
- The state at op 7 CAN be reached via multiple move sequences
- LRU's deterministic state encoding recognizes the duplicate
- Flat_cache's encoding does NOT recognize the same state when reached via a different path
- All seven hashes are unique in the deterministic trace (no collisions)

**Conclusion**: This is a **correctness bug, not a deduplication efficiency issue**. Flat_cache is failing to recognize equivalent game states reached through different move orders. This causes it to re-explore already-visited portions of the game tree.

**Severity**: HIGH — flat_cache is incorrectly returning MISS for states that have been seen before, violating transposition table correctness.

---

## Root Cause

**STARTING(0) descriptor was not position-canonical.**

`init_payload_and_hash()` assigned STARTING(0) to all cards regardless of their
actual tableau position. But `determine_destination_descriptor()` computes
position-based descriptors (ROOT, PARENT_0–3) when a card is placed on a pile.
When a card was moved away and returned to the same position via a different move
sequence, it received a PARENT_x descriptor instead of STARTING(0). The LRU
cache, which rebuilds its representation from scratch on every insert, saw these
as the same state. The flat cache, which relies on incrementally-maintained
descriptors, saw them as different.

**Example (FreeCell seed 1, op 7):** Card 8C starts on 9D with descriptor
STARTING(0). Via one move sequence it never moves (stays STARTING). Via another
sequence it is moved away then returned to 9D, receiving PARENT_1(5) from
`determine_destination_descriptor`. Same board, different payload.

**Fix:** `init_payload_and_hash()` now computes the correct positional descriptor
for every face-up tableau card (ROOT for bottom of pile, PARENT_x for cards
sitting on a parent), matching what `determine_destination_descriptor()` would
compute. STARTING(0) is now only used for face-down cards.

## Design Note: STARTING descriptor and future rule variants

The fix above eliminates the STARTING/PARENT distinction for face-up cards,
which is correct because the LRU cache has no concept of "original position."
However, this assumes that `determine_destination_descriptor()` is the canonical
authority on what descriptor a card should have at any given position.

**Potential edge case for future rules:** If a game variant were introduced where
a card's history matters (e.g., "a card that has never been moved has special
properties"), the descriptor model would need to distinguish "originally placed
here" from "moved back here." This would require either:

1. A dedicated STARTING descriptor that `determine_destination_descriptor()`
   also returns when a card is in its original position (requires tracking
   original positions), or
2. Accepting false negatives (flat cache misses valid cache hits) for such games,
   or
3. A separate mechanism outside the descriptor model to track card history.

No current game variant has this property. This note is for future reference.

## Notes

- The mismatches occur in games without pile symmetry (not suit-symmetry related)
- Somerset seed 1 passes, seed 3 fails (seed-dependent behavior)
- FreeCell op 7 is a genuine correctness bug, not initialization divergence
- Root cause: STARTING(0) was assigned to all cards at init but not returned by determine_destination_descriptor() for the same position
