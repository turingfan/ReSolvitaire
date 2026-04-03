# Predecessor Encoding: A Unified Approach for Extended Flat Cache

**Date:** 2026-04-03
**Branch:** `refactor-caching`
**Status:** Proposal — evaluation of human idea
**Origin:** Ian Gent (human contributions #24, #25)

---

## The Idea

Combine two existing ideas into a single general encoding for all game types where
the current 32-byte descriptor model doesn't work:

1. **Legacy Solvitaire's sequence representation** — cards exist in zones (cells,
   stock, waste, reserve, tableau piles) and can be written in a fixed order for
   comparison. Legacy used explicit card bytes and separator tokens between zones.

2. **Accordion's predecessor encoding** (human contribution #23) — instead of
   encoding each card's absolute position, encode its **predecessor** (what it sits
   on). For Accordion this was the previous visible top card in the linked-list
   sequence. The key insight (human contribution #24) is that this generalises: in
   *any* solitaire game, each card has a predecessor — usually the card below it on
   its pile, but sometimes a zone marker indicating the card's position type (bottom
   of a tableau pile, in a cell, in stock, etc.).

3. **No separators needed** (human contribution #25). Because each card encodes its
   own predecessor, the zone structure is implicit. A card at the bottom of a pile
   has predecessor "IN_SPACE" or a pile-specific marker. A card in a cell has
   predecessor "IN_CELL". There are no explicit separator tokens between zones — the
   predecessor values carry the structural information that legacy Solvitaire encoded
   with separators.

---

## Encoding Format

### Predecessor Array: 52 Values, One Per Card

Each card stores a single value: the ID of its predecessor (what it sits on).
Possible predecessor values:

- **Card ID (0–51):** The card sits on another specific card on a pile.
- **Zone marker:** The card is at a position that has no card predecessor.

### Zone Markers Needed

The predecessor must distinguish positions that have different strategic meaning.
Considering all single-deck game types:

| Zone marker | Meaning | When used |
|---|---|---|
| IN_SPACE | Bottom of a tableau pile (empty space below) | All games with tableau |
| IN_CELL | In a freecell | Games with cells |
| IN_STOCK | In the stock pile | Games with stock |
| IN_WASTE | In the waste pile | Games with waste |
| IN_RESERVE | In a reserve pile | Games with reserve |
| IN_FOUNDATION | On a foundation pile | All foundation games |
| IN_HOLE | On the hole pile | Hole games (Black Hole, etc.) |
| BURIED | Not visible (Accordion: card under another) | Accordion |
| STARTING | Face-down, unmoved (hidden card) | Games with hidden cards |

That's 9 zone markers + 52 card IDs = 61 values. **Fits in 6 bits (64 values).**

Three spare values remain for future use. If any game type requires more
fine-grained distinction (e.g., specific pile identifiers for tableau-dealing
games), we still fit.

Note: cards in stock, waste, reserve, and foundations are often in their starting
positions (human contribution #4). Their predecessors don't change during play,
so the predecessor array for these cards is static — set once at init, never
updated. Only tableau, cell, hole, and accordion predecessors change during search.

### 6-Bit vs 8-Bit Per Card

**6-bit encoding:** 52 × 6 = 312 bits = 39 bytes. Tight packing, slightly awkward
bit manipulation (shift/mask across byte boundaries), but leaves 25 bytes free in
a 64-byte payload.

**8-bit encoding:** 52 × 8 = 52 bytes. Byte-aligned, trivial access
(`predecessor[card_id]`), leaves 12 bytes free in a 64-byte payload. 12 bytes is
still ample for foundation ranks (2 bytes), waste pointer (1 byte), occupied flag
(1 byte), depth (2 bytes), and spare capacity.

**Recommendation:** Start with 8-bit for simplicity. The 12 remaining bytes are
sufficient for all metadata. If space pressure arises (e.g., for two-deck games),
switch to 6-bit packing. For single-deck games, the simpler access pattern is
worth the extra 13 bytes.

### Payload Layout (8-bit, 64 bytes)

```
Byte  0:      occupied flag (0 = empty slot)
Bytes 1–2:    depth counter (excluded from comparison)
Bytes 3–4:    foundation ranks (4 bits × 4 suits) or hole top card
Byte  5:      waste pointer / stock state
Bytes 6–57:   predecessor array (52 × 8 bits = 52 bytes)
Bytes 58–63:  spare (6 bytes)

Comparison region: bytes 3–57 (55 bytes)
```

---

## 128-Byte Bucket Design: Hash-Guarded Second Entry

With 64-byte entries, each bucket holds two entries in 128 bytes (two cache lines).
The current 32-byte design fits two entries in one cache line — with 64-byte entries
we lose that property. However, a clever layout **avoids loading the second cache
line in almost all cases** (human contribution #25):

### Layout

```
Cache line 1 (bytes 0–63):
  Bytes 0–55:   Entry 1 payload (56 bytes: occupied + depth + 52-byte comparison region + spare)
  Bytes 56–63:  Zobrist hash of Entry 2 (8 bytes)

Cache line 2 (bytes 64–127):
  Bytes 64–119: Entry 2 payload (56 bytes)
  Bytes 120–127: Zobrist hash of Entry 1 (8 bytes — for symmetry, optional)
```

### Lookup Protocol

1. **Load cache line 1** (bytes 0–63). This is the mandatory DRAM fetch.
2. **Check Entry 1:** Compare bytes 3–55 against the probe state (53 bytes).
   If match → hit. Done.
3. **Check Entry 2 hash:** Compare the probe's Zobrist hash against bytes 56–63
   (Entry 2's stored hash). **This check is free** — the data is already in L1
   from step 1.
   - If hash mismatch → Entry 2 is definitely not a match. **Done. No second
     cache line fetch.** This is the common case — a random 64-bit hash has a
     ~1/2^64 chance of false match.
   - If hash match → Entry 2 *might* match. Load cache line 2 and do full
     comparison for mathematical correctness.

### Impact

For the typical case (probe misses both entries), this design requires **one DRAM
fetch instead of two**. The hash comparison on the second entry is a single 8-byte
comparison already in L1 — negligible cost. Only when the hash matches (essentially
never for misses, almost always for actual matches) do we pay for the second fetch.

This makes the 128-byte bucket nearly as fast as the current 64-byte bucket for
lookup-dominated workloads, while doubling the payload capacity.

### Entry Payload with Hash Guard

Each entry has 56 bytes of payload (not 64), because 8 bytes per cache line are
used for the other entry's hash. With the 8-bit predecessor encoding:

```
Entry 1 (56 bytes):
  Byte 0:      occupied flag
  Bytes 1–2:   depth (excluded from comparison)
  Bytes 3–4:   foundation ranks or hole top card
  Byte 5:      waste pointer
  Bytes 6–53:  predecessor array (48 cards × 8 bits)
  Bytes 54–55: remaining 4 predecessors (packed) or spare
```

Hmm — 48 cards in bytes 6–53, but we need 52. That's 4 short. Options:

**Option A:** Use 6-bit packing for the predecessor array after all: 52 × 6 = 39
bytes, fitting in bytes 6–44, leaving bytes 45–55 for metadata and spare. This is
the best fit for the hash-guarded design.

**Option B:** Reduce metadata. We need: occupied (1 byte), depth (2 bytes),
foundation (2 bytes), waste (1 byte) = 6 bytes of overhead. Predecessor array =
52 bytes. Total = 58 bytes > 56. So 8-bit encoding doesn't fit with the hash guard
in 56 bytes. **6-bit packing is needed for the hash-guarded design.**

### Revised Layout with 6-Bit Predecessors + Hash Guard

```
Cache line 1 (bytes 0–63):
  Byte 0:      occupied flag
  Bytes 1–2:   depth
  Bytes 3–4:   foundation ranks (4 bits × 4 suits) or hole top card
  Byte 5:      waste pointer
  Bytes 6–44:  predecessor array (52 × 6 bits = 39 bytes)
  Bytes 45–55: spare (11 bytes — enough for any future metadata)
  Bytes 56–63: Zobrist hash of Entry 2

Cache line 2 (bytes 64–127):
  Byte 64:     occupied flag
  Bytes 65–66: depth
  Bytes 67–68: foundation ranks / hole top
  Byte 69:     waste pointer
  Bytes 70–108: predecessor array (52 × 6 bits = 39 bytes)
  Bytes 109–119: spare
  Bytes 120–127: Zobrist hash of Entry 1

Comparison region per entry: 42 bytes (bytes 3–44 / 67–108)
```

This gives: 6-bit predecessor packing, 11 bytes of spare per entry (generous), and
the hash-guard optimisation. The comparison region (42 bytes) is larger than the
current 29-byte comparison but well within acceptable bounds — the DRAM fetch
dominates, not the memcmp.

---

## Suit Symmetry: The Remaining Hard Problem

The predecessor encoding cleanly solves tableau dealing, gaps, and accordion.
**Suit symmetry is harder than initially claimed** and deserves careful analysis.

### What Works

Under colour symmetry (H↔D, S↔C), replace card IDs with equivalence-class IDs:
- AH, AD → "red Ace" (class 0)
- AS, AC → "black Ace" (class 1)
- etc.

Each class has exactly 2 cards. The predecessor of a card is stored as its
equivalence class ID (or zone marker), not its card ID. This means a card "on the
red Ace" doesn't specify which red Ace — and under symmetry, it shouldn't.

**Case analysis for common situations:**

Piles [AH, 2S] and [AD, 2C] (equivalent under H↔D, S↔C):
- Both red Aces: predecessor = IN_SPACE → class predecessors: {IN_SPACE, IN_SPACE} ✓
- Both black 2s: predecessor = red-Ace → class predecessors: {red-Ace, red-Ace} ✓
- Identical encoding. ✓

Card 3H on tableau, 3D on foundation:
- Class "red-3": predecessors = {<tableau-pred>, IN_FOUNDATION}
- Swapping H↔D: 3D on tableau, 3H on foundation → same class predecessors. ✓

### What Needs Careful Handling

**Problem: Two cards in the same equivalence class may have different predecessors.
The encoding must be invariant under permutation within the class.**

For class "red-3" with cards 3H and 3D:
- State A: 3H has predecessor black-4, 3D has predecessor IN_FOUNDATION
- State B: 3D has predecessor black-4, 3H has predecessor IN_FOUNDATION
- These are equivalent under H↔D symmetry.

**Solution: Store each class's predecessors as a sorted tuple.** For a class with
2 members, sort the two predecessor values. State A and State B both become
`{IN_FOUNDATION, black-4}` (sorted). This is canonical.

For suit-irrelevant games (4 equivalent cards per class), sort all 4 predecessor
values per class.

**Zobrist hashing with sorted classes:** Use additive combining (not XOR) so that
the hash is naturally commutative within a class:
`hash += Z[class][pred_1] + Z[class][pred_2]`
Addition is commutative, so the hash is invariant under card permutation within
a class. No need to sort for hashing — only for the payload comparison.

### What Needs Further Analysis

**Foundation metadata canonicalization.** The foundation state (stored in bytes
3–4 as per-suit top ranks) must also be canonicalized under symmetry. Under
colour symmetry, hearts and diamonds foundations are interchangeable. Instead of
`(hearts_rank, diamonds_rank)`, store `(max, min)` or `(sorted pair)`. Similarly
for spades/clubs. This is a separate concern from the predecessor encoding but
must be correct for the full state to be canonical.

**Predecessor values that reference equivalent cards.** If card X sits on card Y,
and Y is in a class with equivalent card Y', then X's predecessor is "class of Y"
(which is the same for Y and Y'). This works. But if X and X' are equivalent and
*both* sit on different members of Y's class, we need the sorted-tuple approach
above to avoid distinguishing which X is on which Y. Case analysis suggests this
works, but a formal argument or exhaustive test on small instances is needed.

**Pile ordering under symmetry.** With suit-reduced predecessor IDs, two piles
that differ only in suit content (e.g., [AS, 2H] and [AH, 2D] in a red-black
game) produce identical predecessor sequences — no pile sorting needed to equate
them. But piles with different rank content (e.g., [AS, 2H] and [3S, 4H]) are
correctly distinguished. Piles with the same suit-reduced content in different
tableau positions still need canonical ordering (pile sorting) to produce
identical payloads. This is the same requirement as legacy Solvitaire, and the
existing `game_state.pile_order.cpp` logic can be adapted.

### Assessment

The predecessor encoding with sorted equivalence-class tuples **appears sound**
for suit symmetry based on case analysis. The approach is:
1. Reduce card IDs to equivalence class IDs
2. Store predecessors using these reduced IDs
3. Sort the predecessor values within each equivalence class (canonical form)
4. Canonicalize foundation metadata (sort within colour groups)
5. Apply pile ordering for tableau (adapt existing logic)
6. Use additive Zobrist combining

This is significantly simpler than the chain model proposed in the original
roadmap, but suit symmetry remains the most complex case and **must be formally
verified or exhaustively tested before implementation**. It is recommended to
implement the non-symmetry cases first (accordion, tableau dealing, gaps) and
tackle symmetry separately, with thorough testing.

---

## Games Covered by This Approach

After implementation, the predecessor encoding covers **every single-deck exclusion**
currently routing to LRU cache:

| Exclusion | Games | Status |
|---|---|---|
| `stock_deal_t == TABLEAU_PILES` | east-haven, spiderette, will-o-the-wisp | Straightforward — pile identity implicit |
| `sequence_count > 0` | gaps-one-deal, gaps-basic-variant | Straightforward — grid positions |
| `accordion_size > 0` | accordion, accordion-knuth, late-binding-solitaire | Original design, well understood |
| `suit_symmetry_active` | any game with suit-symmetry streamliner | Needs sorted-class-tuple approach; requires formal verification |

All single-deck preset game types would use the flat cache.

---

## Comparison with Existing Designs

### vs Legacy Solvitaire's Sequence Representation

| Aspect | Legacy Solvitaire | Predecessor encoding |
|---|---|---|
| Storage | Variable-length `vector<card>` + separators | Fixed 56–64 bytes, no separators |
| Per-card cost | 8 bits (card value) + separator overhead | 6 bits (predecessor ID) |
| Equality check | `vector ==` (variable length) | `memcmp` 42 bytes (fixed) |
| Hash | Boost hash_combine (rebuild every insertion) | Zobrist (O(1) incremental) |
| Pile symmetry | Pile sorting required | Only for suit-symmetry mode |
| Suit symmetry | Suit-reduce cards + pile sort | Reduced IDs + sorted class tuples + pile sort |
| Memory per entry | ~70–90 bytes (vector + heap allocation) | 56–64 bytes (inline, cache-aligned) |

The predecessor encoding preserves legacy Solvitaire's conceptual simplicity
(cards in zones, with structural relationships) while eliminating separators,
gaining fixed-size entries, and enabling incremental Zobrist hashing.

### vs Current Descriptor Model

| Aspect | Descriptor model (32-byte) | Predecessor encoding (64-byte) |
|---|---|---|
| Games covered | Most single-deck, non-symmetric | All single-deck (including extensions) |
| Payload size | 32 bytes (one cache line / 2 entries) | 56 bytes in 128-byte bucket |
| Entries per bucket | 2 per 64 bytes | 2 per 128 bytes (with hash guard) |
| Effective memory cost | Lower for covered games | ~2× for same entry count |
| Incremental hash | O(1) XOR per descriptor change | O(1) XOR/add per predecessor change |

**Recommendation:** Keep the 32-byte descriptor model for games it already handles.
Use the predecessor encoding for the extended games that the descriptor model cannot
cover. The `use_new_cache()` function already routes games to the appropriate cache;
extend it with a third option for predecessor-based flat cache.

---

## Zobrist Hashing Details

### Standard (no symmetry)

Table: `Z_pred[52][64]` — card ID × predecessor value (52 cards + 12 zone markers).
Combine: XOR. All cards distinguishable, no cancellation risk.
Table size: 52 × 64 × 8 = ~26 KB. Fits comfortably in L1.

### Suit symmetry (colour: H↔D, S↔C)

Table: `Z_pred[26][32]` — equivalence class × predecessor class (26 classes + ~6
zone markers). Combine: modular addition. Commutative within class.
Table size: 26 × 32 × 8 = ~6.5 KB. Tiny.

### Suit-irrelevant (4 equivalent per rank)

Table: `Z_pred[13][16]` — rank × predecessor rank/zone.
Combine: modular addition.
Table size: 13 × 16 × 8 = ~1.6 KB.

### Move Update Cost

Single-card move: update 2–3 predecessor entries (moved card, old successor,
destination's old top card). Each update is one XOR-out + one XOR-in (or
subtract/add for symmetric modes). O(1).

Multi-card group move: only the bottom card of the group changes predecessor
(to the new destination's top card or IN_SPACE), and the destination's old top
card changes successor. Interior cards keep their within-group predecessors.
O(1). (Human contribution #7.)

---

## Open Questions

1. **Formal verification of suit symmetry.** The sorted-class-tuple approach
   appears correct from case analysis but needs either a formal proof or
   exhaustive testing on small instances (e.g., 8-card games with 2-way symmetry).

2. **Foundation canonicalization under symmetry.** Must sort foundation ranks
   within colour groups. Straightforward but must be implemented and tested.

3. **Two-deck games.** 104 cards × 6 bits = 78 bytes; × 8 bits = 104 bytes.
   Doesn't fit in 56 bytes. Options: 128-byte payload, or hybrid with chain
   model for tableau + predecessor for other zones. Deferred.

4. **Face-down cards.** Cards that are face-down and unrevealed have STARTING
   as their predecessor and never change. Their predecessor value is static.
   When revealed, the predecessor transitions to IN_SPACE or a card ID depending
   on position. Needs careful testing for Klondike-type games.

5. **Interaction with `use_new_cache()`.** Extend the routing function with a
   third mode: `use_predecessor_cache()` for games excluded from the descriptor
   cache but supported by the predecessor encoding.

---

## Recommended Implementation Order

| Priority | Item | Complexity | Rationale |
|---|---|---|---|
| 1 | Accordion | Low | Original testbed; smallest game type; proves the mechanism |
| 2 | Tableau dealing | Moderate | Most practically impactful; no symmetry complications |
| 3 | Gaps | Moderate | Straightforward positional model |
| 4 | Suit symmetry | Hard | Needs sorted-class-tuples and formal verification |

After items 1–3, the only remaining single-deck LRU users are suit-symmetry
games. Item 4 can proceed once the encoding is proven on the simpler cases and
the symmetry approach is verified.
