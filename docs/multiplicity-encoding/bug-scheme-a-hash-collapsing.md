# Bug Report: Scheme A Predecessor Collapsing Incomplete

**Date:** 2026-05-19  
**Branch:** `multiplicity-encoding`  
**Affects:** `multiplicity_descriptor_engine.h`, `recompute_from_descriptors()`  
**Status:** Both issues fixed

## Summary

The canonicalisation in `recompute_from_descriptors()` does not correctly handle
all suit-symmetric equivalences.  Two issues were found by Stage 2C metamorphic
tests (randomised suit-permutation invariance checks):

1. **Hash ignores Scheme A collapsing** (fixed) — the Zobrist hash looked up
   entries using the raw `canonical_pos[predecessor_card_id]` instead of the
   collapsed slot byte from Scheme A.

2. **Scheme A needs to iterate** (fixed) — Scheme A ran a single pass, but
   collapsing in one static class can create new indistinguishable groups in
   other classes that need their own collapsing.  Fixed by folding Scheme A
   into the fixpoint loop.

## Background: How Canonicalisation Works

Each card has a **descriptor**: either a locative (where it sits — foundation,
cell, stock, space) or a predecessor (which card it sits on top of, plus a
face-down flag).  Each card belongs to a **static class** — cards that are
interchangeable under the active suit symmetry:

- COLOUR mode: 26 classes of 2 (e.g. "black Aces" = {AC, AS})
- SUIT_IRRELEVANT mode: 13 classes of 4 (e.g. "Aces" = {AC, AH, AS, AD})

The engine's goal: two game states that differ only by a valid suit permutation
must produce identical hash and payload.

The algorithm in `recompute_from_descriptors()`:

1. Compute a **slot byte** for each card from its descriptor.  For a predecessor
   descriptor, the slot byte is the `canonical_pos` of the predecessor card.
2. **Fixpoint**: sort each class by (slot byte, card_id), assign `canonical_pos`
   from the sorted order, recompute predecessor slot bytes.  Repeat until stable.
3. **Scheme A collapsing**: within each class, members with equal slot bytes are
   **indistinguishable**.  Any card whose predecessor is one of these
   indistinguishable members gets its slot byte collapsed to the lowest
   `canonical_pos` in that group.
4. Final sort + assign canonical positions.
5. Write the payload from slot bytes.
6. Compute the hash from slot bytes via Zobrist lookups.

## Issue 1: Hash Ignored Scheme A Collapsing (FIXED)

### The problem

`zob_for_card()` computed its Zobrist column as
`canonical_pos[d.predecessor_card_id]` — the raw canonical position of the
predecessor card.  But Scheme A collapses the *slot byte* to use the lowest
canonical position of the predecessor's indistinguishable group.  The hash did
not use the collapsed value.

### Example (COLOUR mode)

AH and AD (red Aces, static class 1) both have locative IN_SPACE — identical
slot bytes, indistinguishable.  The sort tiebreaker assigns AH `canonical_pos=2`
and AD `canonical_pos=3`.

Two suit-permuted states:
- **Original:** TS sits on AH.  Scheme A collapses slot to 2 (lowest of {AH, AD}).
- **Permuted (H↔D):** TC sits on AD.  Scheme A collapses slot to 2 (same).

Payload correct (both slot byte 2).  But the old hash used `canonical_pos[AH]=2`
vs `canonical_pos[AD]=3` as the Zobrist column — different hash.

### The fix

Changed `zob_for_card()` to derive its column from `slot[]` (which includes
Scheme A collapsing) instead of recomputing from `canonical_pos`:

```cpp
uint64_t zob_for_card(uint8_t class_id, uint8_t card_idx) const {
    const auto& d = descriptors[card_idx];
    uint8_t col = d.face_down
        ? static_cast<uint8_t>(255 - slot[card_idx])
        : slot[card_idx];
    uint64_t z = multiplicity_zobrist::Z[class_id][col];
    return d.face_down ? ~z : z;
}
```

Also populated `slot[]` in the NONE mode fast path.  This resolves all COLOUR
mode test failures.

## Issue 2: Scheme A Needs to Iterate (FIXED)

### The problem

Scheme A ran a single pass over all static classes.  But collapsing predecessor
references in one class can make previously-distinguishable members of another
class become indistinguishable, creating new groups that need their own
collapsing.  Both the payload and hash were affected.

### Worked example (SUIT_IRRELEVANT mode)

Suit permutation: C→D→S→H→C (a 4-cycle rotation).

**Kings class** (KC, KH, KS, KD):

| Member | Original descriptor | Permuted descriptor |
|--------|--------------------|--------------------|
| KC(12) | loc(IN_SPACE)      | loc(IN_SPACE)      |
| KH(25) | loc(IN_SPACE)      | pred(8S)           |
| KS(38) | pred(8D)           | loc(IN_SPACE)      |
| KD(51) | loc(IN_SPACE)      | loc(IN_SPACE)      |

In both states, 3 Kings have loc(IN_SPACE) (slot 58) and 1 King has a
predecessor (lower slot).  After sorting, the 3 identical Kings occupy
consecutive positions.  Scheme A correctly identifies them as indistinguishable
and collapses all predecessor references to any of the 3 to `lowest_pos`.

**Aces class** (AC, AH, AS, AD) — each Ace sits on a King:

| Member | Original descriptor | Permuted descriptor |
|--------|--------------------|--------------------|
| AC(0)  | pred(KD)           | pred(KD)           |
| AH(13) | pred(KC)           | loc(IN_SPACE)      |
| AS(26) | loc(IN_SPACE)      | pred(2S)           |
| AD(39) | pred(2D)           | pred(KS)           |

Before Scheme A, AC's slot = `canonical_pos[KD]` and AH's slot =
`canonical_pos[KC]`.  Since KD and KC have different card IDs (51 vs 12), the
tiebreaker gives them different canonical positions, so AC and AH have different
slot bytes.

After Scheme A collapses the Kings class: `pred(KC)`, `pred(KH)`, and `pred(KD)`
all collapse to the same `lowest_pos`.  Now AC and AH both have the same slot
byte — they have become indistinguishable.

But Scheme A has already finished its single pass.  It does not revisit the Aces
class to collapse references to {AC, AH}.  So any card sitting on AC gets a
different slot byte than a card sitting on AH, even though they are now
equivalent.

In the permuted state, Scheme A collapses `pred(KC)`, `pred(KS)`, and
`pred(KD)` — making AC and AD indistinguishable instead.  The downstream effects
cascade differently, and the 3s class ends up with different slot multisets:
{1, 4, 25, 58} vs {2, 4, 25, 58}.

### Why this is not a fundamental limitation

The user's argument applies: at any point in the collapsing process, two class
members either have the same slot byte (indistinguishable — must be collapsed)
or different slot bytes (distinguishable — one is canonically smaller, not due to
an arbitrary tiebreak).  The information needed for correct canonicalisation is
present; it just requires propagation.

This is the same cascading-equivalence issue that was anticipated for Stage 3
(incremental computation).

### The fix

Folded Scheme A into the fixpoint loop rather than running it as a separate pass
afterwards.  The key change: when recomputing predecessor slot bytes during the
fixpoint iteration, use `collapsed_pos(q)` instead of raw `canonical_pos[q]`.

`collapsed_pos(q)` finds the lowest `canonical_pos` among all members of `q`'s
static class that share the same slot byte as `q` — i.e. it returns the
canonical representative of `q`'s indistinguishable group.

```cpp
uint8_t collapsed_pos(uint8_t q) const {
    uint8_t q_cls  = classes.class_of[q];
    uint8_t q_base = classes.class_start[q_cls];
    uint8_t q_slot = slot[q];
    for (uint8_t i = 0; i < classes.class_size; i++) {
        if (slot[classes.class_members[q_base + i]] == q_slot)
            return static_cast<uint8_t>(q_base + i);
    }
    return canonical_pos[q];
}
```

This means the fixpoint loop now propagates collapsing across classes
automatically: if collapsing in the Kings class makes two Aces
indistinguishable, the next fixpoint iteration picks that up via
`collapsed_pos()` when recomputing slots for cards sitting on those Aces.

The old separate Phase 3 (Scheme A) and "final sort + assign" blocks were
removed entirely.  The fixpoint loop limit was increased from 12 to 20
iterations.

### Scope

All three symmetry modes are now correct:
- **NONE mode (1 per class):** Fast path, no fixpoint.
- **COLOUR mode (2 per class):** All tests pass.
- **SUIT_IRRELEVANT mode (4 per class):** All tests pass, including the
  previously-failing seed 19.

## Test Status

- All 13 Stage 2C unit tests pass (6 metamorphic, 7 structural)
- All existing tests (3-gate suite) pass (pending re-verification after fix)
