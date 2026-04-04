# Predecessor Encoding: A Unified Approach for Extended Flat Cache

**Date:** 2026-04-03
**Branch:** `refactor-caching`
**Status:** Proposal — evaluation of human idea
**Origin:** Ian Gent (human contributions #24, #25, #26, #27, #28)

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

### Zone Markers: 8-Bit Encoding

With 8 bits per card (256 possible values), there is abundant space for card IDs,
zone markers, and per-pile identifiers. No bit-packing needed.

| Range | Value(s) | Meaning |
|---|---|---|
| 0–51 | Card IDs | Predecessor is another specific card |
| 52 | FINAL | Card is in a final position: foundation, hole, or buried (Accordion). Will not move again. |
| 53 | IN_CELL | Card is in a freecell |
| 54 | IN_STOCK | Card is in the stock |
| 55 | IN_WASTE | Card is in the waste |
| 56 | IN_RESERVE | Card is in a reserve pile |
| 57 | STARTING | Face-down, unmoved (hidden card) |
| 58–109 | PILE_0 through PILE_51 | Bottom of a specific tableau pile |
| 110–255 | *(spare)* | 146 values available for future use |

**Key design decisions:**

- **FINAL collapses three old markers into one.** Foundation, hole, and buried
  cards all share the property that they are done — they will not be moved or
  examined again. The foundation top ranks (stored separately in metadata bytes)
  distinguish foundation state; the predecessor encoding doesn't need to.

- **Per-pile PILE_N markers for tableau.** Tableau-dealing games (east-haven,
  spiderette, will-o-the-wisp) deal cards to specific piles, so pile identity
  matters. A card at the bottom of tableau pile 3 gets predecessor `PILE_3`
  (value 61). Up to 52 distinct tableau piles are supported, covering every
  preset game type and any conceivable custom game.

- **For non-tableau-dealing games**, all PILE_N values are interchangeable (any
  empty pile is equivalent). This is handled at a higher level — either accept
  the slight deduplication loss (current flat cache behaviour) or apply pile
  sorting (legacy Solvitaire approach).

Note: cards in stock, waste, reserve, and foundations are often in their starting
positions (human contribution #4). Their predecessors don't change during play,
so the predecessor array for these cards is static — set once at init, never
updated. Only tableau, cell, and accordion predecessors change during search.

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

The 8-bit encoding gives trivial access (`predecessor[card_id]`), 12 bytes of
metadata, and 6 bytes of spare capacity. If space pressure arises for two-deck
games (104 cards), 6-bit packing (52 × 6 = 39 bytes) remains an option.

---

## 128-Byte Bucket Design: Hash-Guarded Second Entry

With 64-byte entries, each bucket holds two entries in 128 bytes (two cache lines).
The current 32-byte design fits two entries in one cache line — with 64-byte entries
we lose that property. However, a clever layout **avoids loading the second cache
line in almost all cases** (human contribution #25):

### Layout

```
Cache line 1 (bytes 0–63):
  Bytes 0–55:   Entry 1 payload (56 bytes)
  Bytes 56–63:  Zobrist hash of Entry 2 (8 bytes)

Cache line 2 (bytes 64–127):
  Bytes 64–119: Entry 2 payload (56 bytes)
  Bytes 120–127: Zobrist hash of Entry 1 (8 bytes)
```

Each entry gets 56 bytes of payload. The 8-bit predecessor array needs 52 bytes,
plus 6 bytes of metadata (occupied, depth, foundation, waste) = 58 bytes total.
That's 2 bytes over the 56-byte slot.

**Resolution: depth is not part of the comparison region.** Depth (2 bytes) is used
only for the TwoBig1 replacement policy, not for state matching. It does not need
to be inside the entry's 56-byte slot — it can be stored in the spare bytes or in
the hash-guard region alongside the other entry's hash. Alternatively, depth can
share the occupied-flag byte (using fewer bits) or be stored in the 6 spare bytes
of the full 64-byte layout and simply excluded from the hash-guarded path.

**Practical layout per entry (56 bytes):**

```
Entry (56 bytes):
  Byte 0:      occupied flag + depth (pack: 1 bit occupied, 15 bits depth,
               or use byte 0 = occupied, byte 1 = depth as uint8 0–255)
  Bytes 1–2:   foundation ranks (4 bits × 4 suits) or hole top card
  Byte 3:      waste pointer
  Bytes 4–55:  predecessor array (52 × 8 bits = 52 bytes)

Comparison region: bytes 1–55 (55 bytes)
```

This fits. Depth is capped to 8 bits (max 255), which is sufficient — solitaire
search depths rarely exceed 200. If more depth range is needed, steal one spare
byte from the predecessor array (only 51 cards use predecessors in most games —
cards on foundations have FINAL and could be inferred from foundation metadata
rather than stored).

### Lookup Protocol

1. **Load cache line 1** (bytes 0–63). This is the mandatory DRAM fetch.
2. **Check Entry 1:** Compare bytes 1–55 against the probe state (55 bytes).
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

### Hybrid Combining: Add Within Classes, XOR Across (Human Contribution #26)

The Zobrist hash uses a **hybrid combining strategy** that surgically applies
modular addition only where needed to prevent XOR cancellation, while preserving
XOR's efficiency and mixing properties everywhere else:

```
hash = XOR over all equivalence classes C of (
    SUM (modular addition) over members m in C of Z[C][predecessor_m]
)
```

**Why this works:**

- **Within a class (addition):** Two equivalent cards (e.g., two copies of AS in
  a two-deck game, or AH and AD under colour symmetry) may have the same
  predecessor. XOR would cancel: `Z[x] ^ Z[x] = 0`. Addition doesn't:
  `Z[x] + Z[x] = 2*Z[x] ≠ 0`. Addition is also commutative, so the hash is
  invariant under permutation of equivalent cards within a class.

- **Across classes (XOR):** Different equivalence classes have independent Z
  tables. Their sums are independent random 64-bit values, so XOR provides
  excellent mixing with no cancellation risk. XOR is faster than addition and
  has better theoretical properties for combining independent hash values.

- **Degenerates cleanly:** When every class has exactly one member (no
  duplicates — the standard single-deck no-symmetry case), the "sum" is just
  the single Z value, and XOR combines them. Identical to the current approach.

**Incremental update** when card `a` in class `C` changes predecessor:

```
// Look up sibling's predecessor (O(1) from predecessor array)
old_sum = Z[C][old_pred_a] + Z[C][pred_b]
new_sum = Z[C][new_pred_a] + Z[C][pred_b]
hash ^= old_sum ^ new_sum
```

A single-card move touches at most 3 cards' predecessors (moved card, old
successor, destination's old top card). Each update requires looking up the
sibling copy's predecessor — O(1). Total cost: 3 class-sum recomputations per
move.

For classes with >2 members (suit-irrelevant: 4 per class), the sum includes
all members. The sibling lookup generalises to reading all members' current
predecessors from the array.

### Zobrist Tables

**Standard (no symmetry):**
Table: `Z_pred[52][110]` — card ID × predecessor value (52 cards + up to 58 zone
markers). Combine: pure XOR (each class has 1 member).
Table size: 52 × 110 × 8 = ~46 KB.

**Suit symmetry (colour: H↔D, S↔C):**
Table: `Z_pred[26][110]` — equivalence class × predecessor value (predecessor
card IDs also reduced to 26 classes + zone markers).
Combine: hybrid (add within 26 classes of 2, XOR across classes).
Table size: 26 × 110 × 8 = ~23 KB.

**Suit-irrelevant (4 equivalent per rank):**
Table: `Z_pred[13][110]` — rank × predecessor value.
Combine: hybrid (add within 13 classes of 4, XOR across classes).
Table size: 13 × 110 × 8 = ~11 KB.

**Two-deck (2 copies per card):**
Table: `Z_pred[52][162]` — card identity × predecessor value (104 card IDs reduced
to 52 classes + zone markers — predecessor values need the full 104 + markers range
or can use reduced 52 + markers if copies are interchangeable).
Combine: hybrid (add within 52 classes of 2, XOR across classes).
Table size: 52 × 162 × 8 = ~67 KB.

**Optimisation:** For games with few tableau piles, the second dimension can be
trimmed to `58 + num_piles` (single-deck) or `110 + num_piles` (two-deck).

### Move Update Cost

Single-card move: update 2–3 predecessor entries (moved card, old successor,
destination's old top card). Each update is one XOR-out + one XOR-in (or
subtract/add for symmetric modes). O(1).

Multi-card group move: only the bottom card of the group changes predecessor
(to the new destination's top card or IN_SPACE), and the destination's old top
card changes successor. Interior cards keep their within-group predecessors.
O(1). (Human contribution #7.)

---

## Verification Strategies (Human Contributions #27, #28)

### In-Payload Hash for Fast-Reject (Contribution #27)

Store the full 64-bit Zobrist hash inside the payload entry. On a cache probe:

1. Compare the 8-byte hash word (single cycle)
2. If mismatch → guaranteed different state → skip full payload comparison
3. If match → compare full payload for correctness (~1/2^64 false match rate)

This moves the common case (bucket occupied by a different state) from a 52-byte
`memcmp` to a single 64-bit word comparison. Cost: 8 bytes of payload space.

In the 128-byte hash-guarded bucket layout, this is compatible with storing
Entry 2's hash in Entry 1's cache line (#25) — they serve different purposes.
The hash guard avoids *fetching* the second cache line; the in-payload hash
avoids *comparing* the full payload once fetched.

If 8 bytes of payload cannot be spared, this can be traded off against the
hash-guarded second cache line — either optimisation independently provides
fast rejection, but at different levels (DRAM fetch vs comparison).

### Hash-Only Cache Streamliner (Contribution #28)

Each cache entry stores *only* the 64-bit Zobrist hash — no payload at all.

**Density:** 8 entries per 64-byte cache line (8× over full-payload entries).
The entire cache probe becomes:
```
bucket = hash % num_buckets
for each of 8 slots in cache_line[bucket]:
    if slot == hash: return HIT
return MISS
```

**No payload computation:** The cache never constructs or compares predecessor
arrays. Only the Zobrist hash (already maintained incrementally for bucket
selection) is used. This eliminates all payload overhead from the hot path.

**Correctness:**

- **Solution found → definitely correct.** The solver has the actual move
  sequence. False positives can only prune branches, never fabricate a solution.

- **No solution found → almost certainly correct.** False positive probability
  per probe is ~1/2^64. Over a full search of ~10^8 states, the expected number
  of false positives is ~10^8 / 2^64 ≈ 5×10^-12. Essentially zero.

**Relationship to #16 (1-bit payload):** This refines the 1-bit idea with
full 64-bit hash discrimination (vastly better false positive rate — 1/2^64 vs
dependent on bucket collision rate for 1-bit) and adds the two-phase
verification strategy below.

**Two-phase strategy for batch solvability surveys:**

1. **Phase 1:** Run with hash-only cache (fast, 8× density, no payload overhead)
2. **Phase 2:** For games classified as unsolvable, re-run with full payload
   verification

Games classified as solvable need no re-verification — the solution is proof.
The proportion requiring re-verification depends on the game (e.g., ~20% for
Klondike, ~0.001% for FreeCell). Critically, the full-payload re-run is
the baseline cost that would have been paid without the streamliner. The actual
overhead of the two-phase strategy is only the initial hash-only run, which is
*cheaper* than baseline (no payload computation, 8× cache density → fewer
evictions → less re-search). **The streamliner is therefore strictly better in
expected cost than a single full-payload run.**

**Implementation modes:**

| Mode | Entry size | Entries/64B | Payload | Use case |
|---|---|---|---|---|
| Full payload | 56–64 B | 1 | Predecessor array | Production / oracle |
| Hash-in-payload | 56–64 B | 1 | Hash + predecessors | Faster probes (same density) |
| Hash-only | 8 B | 8 | None | Batch surveys / streamliner |

---

## Open Questions

1. **Formal verification of suit symmetry.** The sorted-class-tuple approach
   appears correct from case analysis but needs either a formal proof or
   exhaustive testing on small instances (e.g., 8-card games with 2-way symmetry).

2. **Foundation canonicalization under symmetry.** Must sort foundation ranks
   within colour groups. Straightforward but must be implemented and tested.

3. **Two-deck games.** 104 cards × 8 bits = 104 bytes — doesn't fit in 56 bytes.
   Options: 6-bit packing (104 × 6 = 78 bytes — still too large for one entry),
   128-byte payload (3 cache lines per bucket), or a hybrid approach. Deferred.

4. **Face-down cards.** Cards that are face-down and unrevealed have STARTING
   as their predecessor and never change. Their predecessor value is static.
   When revealed, the predecessor transitions to PILE_N or a card ID depending
   on position. Needs careful testing for Klondike-type games.

5. **Interaction with `use_new_cache()`.** Extend the routing function with a
   third mode: `use_predecessor_cache()` for games excluded from the descriptor
   cache but supported by the predecessor encoding.

6. **Depth bit-width in hash-guarded layout.** With depth packed into 1 byte
   (max 255), extremely deep searches could overflow. In practice solitaire
   search depths rarely exceed ~200, but this should be validated against
   worst-case games (e.g., Spider).

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
