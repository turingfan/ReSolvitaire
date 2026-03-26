# Bug Report: Dual Cache Mismatches

## Summary

5 dual_cache unit tests fail with `LRU=HIT, Flat=MISS` mismatches occurring pre-eviction. In all cases, the lru_cache finds and recognizes states that the flat_cache misses, causing different behavior during search.

**Status**: Root cause unknown. Outcome tests pass (correct solutions reached) but node counts differ.

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

## Notes

- The mismatches occur in games without pile symmetry (not suit-symmetry related)
- Somerset seed 1 passes, seed 3 fails (seed-dependent behavior)
- FreeCell op 7 is a genuine correctness bug, not initialization divergence
- Root cause likely: flat_cache descriptor model doesn't canonicalize equivalent states reached via different move orders
