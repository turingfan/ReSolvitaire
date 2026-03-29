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

## Chain-Based State Representation

**Applies to:** Items 2a, 2b, 5, 6 (all cases with equivalent cards)
**Related literature:** Closest to pattern database projections in automated planning
and puzzle solving, where abstract states group equivalent concrete states.

### Core Insight

In build-sequence solitaire games, a chain on the tableau is fully determined by
its **top card** (rank + colour/symmetry class) and its **length**. In a red-on-black
build game, a chain topped by red-3 with length 4 must be: red-3, black-4, red-5,
black-6. We don't need to know which specific cards are in the chain — the build
rules determine them.

This means the state can be represented as:
1. **A multiset of chains**, each described by (top card rank+colour, length, base
   position — IN_SPACE, specific foundation, etc.)
2. **A multiset of non-chain card locations** — for each (rank, colour/symmetry
   class), how many copies are in each zone: reserve, stock, foundation, or
   "in a chain" (which doesn't need further specification since the chain
   description captures it)

### Why This Solves the Descriptor Ambiguity Problem

The "which equivalent parent is this card on" question dissolves. We never ask
"where is card X" — we ask "what chains exist." Two states with the same multiset
of chains and the same distribution of non-chain cards are identical, regardless of
which physical cards (suits) are involved.

### Concrete Flat Cache Representation (draft — needs validation)

For a single-deck game with colour symmetry (2 equivalence classes × 13 ranks =
26 equivalence classes, 2 cards per class):

**Chain entries (variable count, up to ~13 chains on tableau):**

Each chain needs:
- Top card rank: 4 bits (1–13)
- Top card colour class: 1 bit (red/black) — or more bits for finer symmetry
- Chain length: 4 bits (1–13 typically)
- Base position: 3 bits (IN_SPACE + which foundation group, or similar)
- Total: ~12 bits per chain

With at most ~13 chains on the tableau, that's ~156 bits = ~20 bytes for chain data.

**Non-chain card zone counts:**

For each of the 26 equivalence classes, we need to know how many copies (0, 1, or 2)
are in each non-tableau zone (reserve, stock, waste, foundation). Since there are
only 2 copies per class, this is 2 bits per class per zone.

With 4 zones × 26 classes × 2 bits = 208 bits = 26 bytes.

**Total payload estimate:** ~46 bytes. Fits in 64-byte payload.

For suit-irrelevant games (13 equivalence classes, 4 copies per class): similar
structure, slightly different bit widths for counts (need 3 bits for 0–4 copies).

For two-deck games (52 equivalence classes, 2 copies per class): more chain entries
possible, but same structure. May be tighter on 64 bytes.

### Chain Sorting for Canonical Payload

Chains must be stored in a canonical (sorted) order within the payload so that
two states with the same chains in different pile positions produce identical
payloads. Sort by (base_position, top_rank, top_colour, length) or similar
deterministic ordering.

### Zobrist Hashing for Chain-Based State

**Additive hash** (modular addition) combining:
- Per-chain: `Z_chain[top_rank][top_colour][length][base_position]`
- Per-zone-count: `Z_zone[rank][colour][zone][count]`

Addition makes the hash invariant under chain ordering (commutative). Incremental
updates on a move:
- Subtract old chain hash for affected chain(s)
- Subtract old zone count hashes for affected equivalence classes
- Update chain descriptions and zone counts
- Add new hashes

Typically touches 1–2 chains per move. The chain hash table
`Z_chain[13][2][14][8]` is small (~2912 entries × 8 bytes = ~23 KB).

### Move Cost

A single-card move (card from top of chain A onto chain B, or from reserve to
tableau, etc.):
- Source chain: length decreases by 1 (or chain disappears if length was 1).
  New top card is determined by build rules (the card that was below the moved card).
- Destination chain: length increases by 1, top card changes to moved card.
  Or a new chain of length 1 is created if placed in an empty space.
- Zone count: update for the moved card's equivalence class if changing zones.

Each of these is a subtract-old-add-new on the additive hash. O(1) per move.

Re-sorting the chain list in the payload after a move: O(k log k) where k = number
of chains. Since k ≤ ~13 (single deck) or ~26 (two deck), this is effectively O(1).

### Chain Encoding Cost Depends on Build Rules and Equivalence

**2a (colour symmetry streamliner, single deck):** Builds are opposite-colour.
Chain contents are fully determined by top card (rank + colour) + length, because
the colour alternates and there's only one card of each (rank, suit) in the deck.
The streamliner defines the equivalence classes (H↔D, S↔C). Chain encoding is
compact: ~12 bits per chain.

**Suit-irrelevant games (2b):** All suits are equivalent. A chain topped by rank 3
with length 4 in a rank-only build game has contents fully determined (3, 4, 5, 6
by rank). Very compact.

**Two-deck, same-suit builds (e.g., Forty Thieves):** Chain contents are fully
determined by top card (rank + suit) + length, because each card in the chain
shares the suit. Two copies of the same chain (from duplicate cards) are handled
by the multiset. Compact encoding: ~12 bits per chain.

**Two-deck, opposite-colour builds:** This is the hard case. A chain 4S-3H and
4S-3D are **different chains** (different specific cards), but 4S-3D from copy 1
and 4S-3D from copy 2 are the **same chain**. Chain contents are NOT determined
by top card + length alone — we need the specific suit of each non-top card in
the chain. However, rank is always implied by position (build sequences decrease
by 1), so we only need the **suit** of each non-top card: 2 bits per card (or
1 bit for opposite-colour where only the within-colour choice matters).

**Chain encoding cost summary (top card 6 bits + length 4 bits + base 3 bits
= 13 bits fixed, plus per non-top card):**

| Build type | Bits per non-top card | Chain length 8 total |
|---|---|---|
| Same-suit | 0 (suit = top's suit) | 13 bits |
| Opposite-colour, colour symmetry (2a) | 0 (colour alternates) | 13 bits |
| Opposite-colour, two-deck | 1 (which suit within colour) | 20 bits |
| Any-suit | 2 (full suit) | 27 bits |

**Note:** Item 2a avoids this problem entirely because the streamliner defines
equivalence at the colour level, making it the clean proving ground for the chain
model before tackling the harder two-deck cases.

### Other Open Questions

- **Exact bit layout** within the 64-byte payload for each game category
- **Validation** that the chain representation is state-complete — no two genuinely
  different game states produce the same chain multiset + zone counts. Needs formal
  argument or exhaustive testing for small cases.

---

## Recommended Implementation Order

| Priority | Item | Complexity | Rationale |
|---|---|---|---|
| 1 | Tableau dealing | Moderate | No symmetry complications; cleanest extension |
| 2 | Streamliner hashing (2a) | Hard | Proves chain model + additive hash; removes LRU fallback |
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
