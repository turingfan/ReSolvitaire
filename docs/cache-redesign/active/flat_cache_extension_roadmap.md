# Flat Cache Extension Roadmap

**Date:** 2026-03-29
**Branch:** `refactor-caching`
**Status:** Planning

This document records the design discussion and roadmap for extending the flat cache
to cover all game types currently excluded by `use_new_cache()`.

---

## Current State

The flat cache (descriptor-based Zobrist hash, 32-byte payload, two-entry buckets)
covers most single-deck games. The following are excluded:

| Exclusion condition | Affected single-deck games | Roadmap item |
|---|---|---|
| `stock_deal_t == TABLEAU_PILES` | east-haven, spiderette, will-o-the-wisp | #1 Tableau dealing |
| `suit_symmetry_active` | any game with `--streamliners suit-symmetry/both` | #2 Symmetry |
| `sequence_count > 0` | gaps-one-deal, gaps-basic-variant | #3 Gaps |
| `accordion_size > 0` | accordion, accordion-knuth, late-binding-solitaire | #4 Accordion |
| `two_decks` | 13 games (spider, forty-thieves, etc.) | #6 Two-deck (deferred) |

Items 1–4 would cover **all single-deck preset game types**. Only two-deck games
would remain on the LRU cache.

---

## Shared Design Decision: 64-Byte Payload

The current flat cache uses a 32-byte payload (52 cards × 4-bit descriptors + waste
pointer + stock info). The extensions below all require richer per-card encodings
(~6 bits per card), giving ~39 bytes minimum. The design decision is to move to a
**64-byte payload** for all extended game types, keeping the two-entry bucket structure
(128 bytes per bucket, two cache lines).

This is less cache-efficient than the current 64-byte bucket but keeps the codebase
uniform across all flat cache variants. The capacity formula becomes
`num_buckets = cache_capacity_bytes / 128`.

---

## Item 1: Tableau Dealing (Spider-type stock)

**Games:** east-haven, spiderette, will-o-the-wisp
**Difficulty:** Moderate
**Dependencies:** None

### Problem

Spider-type stock dealing distributes one card to each tableau pile simultaneously.
This breaks the pile symmetry that makes the descriptor model pile-order invariant —
pile identity matters because the deal targets specific piles.

### Design

**Payload (~52 × 6 bits):** For each card on the tableau, encode what card it sits
on (parent card ID, 6 bits). For in-space cards (bottom of pile), encode which pile
they are on (pile index). Stock state encoded as a deal count or pointer into the
fixed deal order.

**Zobrist:** `Z[card_id][parent_card_or_pile_id]`. Standard XOR — cards are fully
distinguishable (single deck, suits matter), so no cancellation issues. Multi-card
deal operation requires XOR out/in for each affected card but is straightforward.

**Move updates:** O(1) per single-card move. Stock deal is O(k) where k = number
of tableau piles (one card dealt per pile).

### No symmetry complications

Single-deck, suits matter, no equivalent cards. Cleanest first extension.

---

## Item 2: Suit/Colour Symmetry

**Games:** any game run with `--streamliners suit-symmetry/both`; suit-irrelevant
games (Black Hole, etc.)
**Difficulty:** Hard (multiple sub-problems)
**Dependencies:** Shares XOR-cancellation solutions with #6 (two-deck)

### Two distinct sub-problems

#### 2a. Streamliner-compatible hashing (hard — proving ground for 2b and #6)

For games where suits matter for mechanics but the streamliner prunes symmetric
branches: the cache needs to produce identical hashes for suit-symmetric states.

**Zobrist approach — additive hash with colour-class indexing:**
- Zobrist table indexed by `Z[rank][colour][descriptor]` (not card ID)
- AH and AD both use `Z[Ace][red][descriptor]`; AS and AC both use `Z[Ace][black][descriptor]`
- Combine all contributions via **modular addition** instead of XOR
- Two equivalent cards with the same descriptor contribute `2 * Z[...]` — no
  cancellation (unlike XOR where identical contributions cancel to zero)
- Undo via subtraction (still O(1))
- Fully additive across all cards for simplicity; collision rate ~1/2^64 is sufficient

This approach is chosen deliberately to validate the additive hash mechanism for
reuse in 2b (suit-irrelevant, 4 equivalent copies per rank) and #6 (two-deck,
2 identical copies per card). Solving XOR cancellation here provides the foundation
for all equivalent-card scenarios.

**Open problem — descriptor ambiguity under equivalence (needs more thought):**

The current PARENT_0 through PARENT_3 descriptors distinguish *which copy* of the
parent rank a card sits on (e.g., PARENT_0 = on AS, PARENT_1 = on AH). Under colour
symmetry, AS and AH are equivalent, creating a fundamental tension:

- **If we keep PARENT_0/1 distinction:** The descriptor breaks symmetry — "on the
  first red Ace" vs "on the second red Ace" is a distinction that shouldn't exist
  under suit equivalence. Two symmetric states get different hashes.
- **If we merge into a single PARENT:** Two cards on different equivalent parents
  get the same descriptor, and the state is no longer uniquely defined — we can't
  tell "two cards on the same Ace" from "two cards on different Aces."

This is the core open question: the descriptor model assumes parents are
distinguishable, but equivalence makes them interchangeable. Possible directions:

- Encode parent relationships as multiset counts rather than per-card assignments
  (e.g., "2 cards on red-Ace parents" not "card X on first Ace, card Y on second")
- Use a canonical ordering within equivalence classes (assign PARENT_0 to whichever
  equivalent parent has more children, or by some other deterministic rule) —
  but maintaining this incrementally through moves/undos may be complex
- Accept that the payload cannot be a per-card descriptor array for symmetric games
  and design a different payload format

This problem must be solved before 2a can be implemented. The solution will directly
apply to 2b and #6.

**Impact:** Removes the current LRU fallback for suit-symmetry games. Validates
the additive hash and descriptor-under-equivalence solutions for all later items.

#### 2b. Suit-irrelevant games (hard)

For games like Black Hole where suit is genuinely meaningless — AS, AH, AD, AC are
functionally identical. This is structurally the same as having 4 copies of each rank,
similar to the two-deck problem.

**Why the descriptor model breaks:** If 5S and 5H are equivalent, then "card X sits
on 5S" and "card X sits on 5H" describe the same state. The parent relationship no
longer uniquely identifies a state because multiple parents are equivalent.

**Why STARTING descriptors reduce hits:** In Black Hole, two piles both containing
[A, 5, 3] (by rank, suits differ) are strategically identical. STARTING descriptors
tag cards by identity, preventing this recognition. Even with suit-blind hashing, the
descriptor model cannot canonicalize across equivalent piles.

**Approach — additive across piles, XOR within piles, rank-indexed:**
- Each pile has a sub-hash computed by XOR within the pile:
  `pile_hash = XOR over visible cards of Z[rank][position_in_pile]`
- Total hash = SUM (modular addition) of all pile sub-hashes
- Addition is commutative, making the hash pile-order invariant without sorting
- Same-content piles (by rank sequence) produce identical sub-hashes automatically
- Suit-blind by construction (indexed by rank, not card ID)
- Moves are O(1): subtract old pile_hash, update, add new pile_hash

**Payload:** Needs pile-sorted canonical form — not descriptor-based. Encode pile
contents as rank sequences in canonical pile order.

**Open question:** Exact payload format needs further design. The additive Zobrist
scheme handles hashing cleanly, but the 64-byte payload must also be
canonical with respect to both suit equivalence and pile equivalence.

### Inherent symmetries in standard games

Even without the streamliner, red-black build games have inherent symmetries of
order 8: {H<->D} x {S<->C} x {red<->black colour swap}. Solvitaire currently
exploits only the within-colour swaps via the streamliner, not the cross-colour
swap.

Exploiting inherent symmetries in the cache runs into the same equivalent-card / XOR
cancellation problem as suit-irrelevant games. Deferred until after 2b and/or #6
validate an approach.

---

## Item 3: Gaps

**Games:** gaps-one-deal, gaps-basic-variant
**Difficulty:** Moderate
**Dependencies:** None

### Problem

Gaps has a fixed grid of positions. Cards move into empty gaps (spaces) if they
match the card to the left. The state is a permutation of cards across grid positions
plus the locations of the gaps.

### Design

**Payload (52 × 6 bits):** Each card encodes its grid position. Gaps encoded as
a marker value at the positions they occupy, or stored separately.

**Zobrist:** `Z[card_id][grid_position]` for cards, `Z_gap[gap_position]` for gaps.
Standard XOR — all cards are distinguishable.

**Move updates:** O(1) — card leaves one position, enters another (XOR out old, XOR
in new; update gap positions).

**Redeals:** Gaps has redeals (shuffling unplaced cards). This requires a full rehash
of all repositioned cards. Since redeals are rare (at most 2–3 per game), this is
acceptable.

---

## Item 4: Accordion

**Games:** accordion, accordion-knuth, late-binding-solitaire
**Difficulty:** Moderate
**Dependencies:** None

### Problem

Accordion starts with 52 piles of one card each. Piles merge when adjacent (or
near-adjacent) pile tops match by rank or suit. The number of active piles shrinks
during play. The goal is to merge everything into one pile.

### Key Insight: Only Top Cards Matter

Once a card is buried (another card merges on top of it), it is strategically
irrelevant — it can never be played or moved again. The entire game state is fully
captured by the sequence of top cards and their ordering. Buried cards disappear
from the state representation entirely.

### Design

**Payload (52 × 6 bits):** Each card stores the ID of its **predecessor** in the
visible sequence (the top card immediately to its left). Buried cards are set to 0
(a marker meaning "not in the sequence"). This encodes a linked list of the visible
top-card sequence.

**Zobrist:** `Z[card_id][predecessor_card_id]`. Standard XOR.

**Move updates — four changes per move:**

When card A (top of its pile) merges onto card B (an adjacent/near-adjacent top card,
matching by rank or suit), A lands on top of B, burying B:

1. **B** is buried: set predecessor to 0 (XOR out old `Z[B][B_pred]`)
2. **A** moves to B's position: predecessor changes from A's old predecessor to B's
   old predecessor (XOR out old `Z[A][A_pred]`, XOR in new `Z[A][B_pred]`)
3. **Card to A's right** (if any — the top card that had A as its predecessor):
   predecessor changes from A to A's old predecessor, closing the gap
   (XOR out old `Z[right][A]`, XOR in new `Z[right][A_pred]`)
4. **Card to B's right** (if any — the top card that had B as its predecessor):
   predecessor changes from B to A, since A now occupies B's position
   (XOR out old `Z[B_right][B]`, XOR in new `Z[B_right][A]`)

Four cards updated, O(1) per move. No pile contents to track, no positional indices
to shift.

---

## Item 5: Suit-Irrelevant Games — Open Design Question

Covered under Item 2b above. The additive-across-piles / XOR-within-piles /
rank-indexed scheme handles hashing, but the exact payload format for canonical
state representation needs further design work. This is the hardest single-deck
extension.

---

## Item 6: Two-Deck Games (Deferred)

**Games:** spider, forty-thieves, lucas, maria, limited, streets, rank-and-file,
american-toad, gargantua, gargantua-redeal, ultra-klondike-2-deck, mrs-mop
**Difficulty:** Hard
**Dependencies:** Shares XOR-cancellation problem with #2b (suit-irrelevant) and
inherent symmetries

### Core Problem

Two copies of each card. XOR-based Zobrist breaks when both copies have the same
descriptor — their contributions cancel to zero, creating false positives.

### Candidate Approaches

1. **Additive hash:** Replace XOR with modular addition for combining card
   contributions. `a + a != 0` so duplicates don't cancel. Undo via subtraction.
   Similar approach to suit-irrelevant games.

2. **Pair hashing:** For each card identity (rank+suit), compute a symmetric
   function of the two copies' descriptors:
   `f(d1, d2) = Z1[min(d1,d2)] ^ Z2[max(d1,d2)]`
   Handles interchangeability and avoids cancellation.

3. **Canonical copy assignment:** `Z[card_id][copy_number][descriptor]` with a
   deterministic rule for which physical card is copy 0 vs 1. Maintaining canonical
   assignment incrementally across moves and undos is complex.

Deferred until lessons from items 1–5 validate the architecture. Approach 1
(additive) is the most likely candidate given its use in item 2b.

---

## Recommended Implementation Order

| Priority | Item | Complexity | Rationale |
|---|---|---|---|
| 1 | Tableau dealing | Moderate | No symmetry complications; cleanest extension |
| 2 | Streamliner hashing (2a) | Moderate | Removes current LRU fallback; immediate benefit |
| 3 | Gaps | Moderate | Straightforward positional model |
| 4 | Accordion | Moderate | Elegant predecessor-based model |
| 5 | Suit-irrelevant (2b) | Hard | Shares XOR problem with #6; needs payload design |
| 6 | Two-deck | Hard | Deferred; builds on lessons from all above |

After items 1–4, all single-deck preset game types are covered by the flat cache.
Items 5–6 address the remaining theoretical coverage gaps (suit-irrelevant games
and two-deck games).

---

## Coverage After Full Roadmap

| Milestone | Games covered by flat cache |
|---|---|
| Current (M8) | All single-deck, non-accordion, non-gaps, non-tableau-deal, non-suit-symmetry |
| After #1 | + east-haven, spiderette, will-o-the-wisp |
| After #2a | + all games with suit-symmetry streamliner |
| After #3 | + gaps-one-deal, gaps-basic-variant |
| After #4 | + accordion, accordion-knuth, late-binding-solitaire |
| After #5 | + suit-irrelevant games (Black Hole, etc.) |
| After #6 | + all two-deck games |
