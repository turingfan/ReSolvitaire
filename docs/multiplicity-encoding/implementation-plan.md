# Detailed Implementation Plan: Multiplicity Encoding v4

**Version:** 1.0
**Date:** 13 May 2026
**Authors:** Ian Gent & AI Assistant (Claude Opus 4.6)
**Status:** Draft — approved for Stage 0.1 investigation; later stages pending review.

## Reference Documents

- `01-Knowledge-Base/Design-Documents/multiplicity_encoding_v4.tex` — specification (v4 final)
- `01-Knowledge-Base/Implementation-Plans/SymmetryPayloadPlan 20260512155752.md` — Stages 1–2 task breakdown (written without code access)
- `02-Code-Repositories/claude-ReSolvitaire/docs/cache-redesign/active/implementation_plan_v4.md` — existing flat cache milestones (M0–M8)
- `02-Code-Repositories/claude-ReSolvitaire/CLAUDE.md` — build/test/architecture reference

## Context

The multiplicity encoding v4 specification and the SymmetryPayloadPlan were written with
major input from Claude (online), but without access to the actual codebase. This document
bridges the gap: it maps the v4 design onto the real code, identifies architectural
prerequisites, and provides a stage-by-stage plan grounded in actual file paths, types,
and interfaces.

### What Already Exists (Milestones 0–6, `dev` branch)

The solver already has a mature flat cache system:

- **Policy-templated architecture.** `game_state_impl<Policy>` is parameterised at compile
  time. Four policies exist: `FlatPolicy`, `HashOnlyPolicy`, `PredecessorPolicy`,
  `LRUPolicy`. Zero virtual dispatch in the DFS hot path.
- **Descriptor-aligned Zobrist hashing.** `Z_card[52][16]` table (~8 KB), XOR-based
  incremental updates on make/undo. Per-card descriptors use a 10-value enum
  (`card_descriptor` in `descriptor.h`).
- **32-byte compact payload.** Nibble-packed per-card descriptors + foundation tops +
  waste pointer + hole top. Comparison via `memcmp` over bytes 3–31.
- **Flat cache.** `generic_flat_cache<ClusterPolicy>` with 2-slot clusters (64 bytes for
  CompactState, 16 bytes for HashOnly), TwoBig1 depth-preferred replacement, lazy mmap
  allocation.
- **Predecessor path.** Accordion games use `PredecessorPolicy` with a 64-byte
  predecessor-encoded state — closest existing analogue to the multiplicity encoding.

### The Fundamental Difference

The current system describes each card's state with a small enum (`STARTING`, `IN_CELL`,
`PARENT_0`–`PARENT_3`, `IN_SPACE`, `ROOT`, `IN_HOLE`). The PARENT_i values identify
*which* legal parent the card sits on (by a fixed suit ordering in `parent_table`).

The multiplicity encoding replaces this with `predecessor(card_id, face_down)` — the card
records *the actual card it sits on*, not which numbered legal parent that is. This enables
suit-symmetry canonicalisation (cards in the same static equivalence class can be freely
permuted), which the current flat cache cannot do (it falls back to LRU for symmetric
games).

### Design Decisions

1. **Coexistence now, replacement later.** The multiplicity cache is introduced as a new
   policy alongside existing ones. This allows (a) regression testing against existing
   policies and (b) performance comparison. However, the long-term intent is that
   `MultiplicityPolicy` will eventually *replace* `PredecessorPolicy` entirely — once
   `MultiplicityPolicy` can encode accordion-style predecessor relationships (the same
   thing `PredecessorPolicy` does for accordion games), `PredecessorPolicy` becomes
   redundant and can be deleted. The dual-system redundancy during the transition is
   an accepted temporary state. Any agent working in this area should not invest effort
   in improving or extending `PredecessorPolicy`.

2. **From-scratch first.** Stages 1–2 use from-scratch payload/hash recomputation on every
   cache operation. Performance will be poor but correctness is provable. Incremental
   updates follow in Stages 4–5 after a separate specification document (Stage 3).

3. **Single-deck only.** Two-deck is deferred entirely.

---

## Stage 0: Architecture Preparation

**Goal:** Refactor `game_state_impl` so that descriptor computation, hash update, and
payload generation are delegated to the policy rather than hardcoded in `game_state.cpp`.
This is the prerequisite that makes the multiplicity encoding a clean plugin.

### Current State of Coupling

All descriptor logic in `game_state.cpp` is guarded by `if constexpr
(Policy::computes_hash)`, but the *content* of those blocks is not policy-parameterised.
They all use the same `card_descriptor` enum, the same `parent_table`, and the same
`zobrist_hash::card_key()`. A new policy enters the same `if constexpr` branches but
needs completely different logic inside them.

Key locations in `game_state.cpp` (line numbers approximate, on `dev` branch):

| Function | Lines | What it does |
|---|---|---|
| `init_payload_and_hash()` | 1152–1243 | Walks piles, assigns initial descriptors using `card_descriptor` enum and `parent_table` |
| `determine_destination_descriptor()` | 1310–1360 | Decides descriptor for a card moved to a destination pile |
| `update_card_descriptor()` | 1246–1255 | XOR delta on `zobrist_hash_value` using `zobrist_hash::card_key()` |
| `update_foundation_in_hash()` | 1257–1267 | XOR delta for foundation top rank |
| `update_waste_ptr_in_hash()` | 1280–1289 | XOR delta for waste pointer |
| `update_hole_top_in_hash()` | 1291–1301 | XOR delta for hole top card |
| `compute_hash_from_scratch()` | 1370–1395 | Recomputes hash by walking `desc_store` |
| `make_regular_move()` | 470–535 | Calls `determine_destination_descriptor` + update functions |
| `undo_regular_move()` | 538–616 | Reverse updates using `card_descriptor::STARTING` etc. |
| `make_built_group_move()` / undo | 620–750 | Similar pattern for multi-card moves |

### Stage 0.1 — Audit and Catalogue the Descriptor Interface

**Task:** Walk every `if constexpr (Policy::computes_hash)` block in `game_state.cpp` and
produce a complete list of operations that a descriptor engine must provide. This is
investigation only — no code changes.

**Expected interface (to be confirmed by audit):**

```
init(game_state)                           — from-scratch initialisation
on_card_moved(cid, dest_pile, game_state)  — descriptor update for moved card
on_card_revealed(cid, pile, game_state)    — face-down to face-up transition
on_foundation_changed(suit, new_rank)      — foundation header update
on_hole_changed(new_top_cid)               — hole header update
on_waste_ptr_changed(new_ptr)              — waste pointer update
get_hash()                                 — current Zobrist hash
get_payload()                              — current payload for cache comparison
recompute_hash()                           — verify/recompute hash from payload
```

**Open question:** Should these be static methods on the Policy struct, or a separate
"descriptor engine" class that the policy typedefs? The latter is cleaner if the engine
has state (which the multiplicity engine will, for per-class slot lists and Zobrist sums).
The audit should inform this choice.

**Deliverable:** A document listing every descriptor operation, its current implementation,
and how it maps to the proposed interface. Plus a recommendation on static-methods vs
engine-class.

### Stage 0.2 — Extract Current Logic into FlatDescriptorEngine

**Task:** Move existing descriptor logic out of `game_state.cpp` into a class that
`FlatPolicy` and `HashOnlyPolicy` reference via a typedef. The `if constexpr` blocks in
`game_state.cpp` become calls to the engine.

**This is a pure refactor — no behaviour change.** Files affected:
- `game_state.cpp` — replace inline descriptor logic with engine calls
- New file: `flat_descriptor_engine.h` (or similar)
- `cache_policy.h` — add engine typedef to `FlatPolicy`, `HashOnlyPolicy`
- Possibly `compact_state.h`, `hash_descriptor_store.h` — if the engine wraps them

**Validation:** All three test gates (release, trace, debug) must pass. All regression
levels that currently pass must still pass. No performance regression expected (engine
methods should inline at `-O3`).

### Stage 0.3 — Validate PredecessorPolicy Compatibility

**Task:** The predecessor path (accordion games) already has somewhat separate logic
(guarded by `uses_predecessor_cache()`). Verify that the extraction in 0.2 doesn't break
it. If predecessor logic also needs extraction into a `PredecessorDescriptorEngine`, do
it now.

**Deliverable:** `game_state.cpp` move/undo methods delegate all descriptor work to the
policy-provided engine. All existing tests pass. No new functionality.

---

## Stage 1: From-Scratch Multiplicity, No Symmetry

**Goal:** Implement the v4 encoding as a from-scratch cache for single-deck no-symmetry
games. Validate against the existing flat cache.

**Prerequisite:** Stage 0 complete.

### 1.1 — New Types

**`multiplicity_descriptor.h`** — the v4 internal descriptor:

```cpp
// Naming convention:
//   MLD_ = Multiplicity Locative Descriptor (card described by its location)
//   MPD  = Multiplicity Predecessor Descriptor (card described by what it sits on)

enum multiplicity_locative : uint8_t {
    MLD_IN_CELL    = 0,
    MLD_PERMANENT  = 1,   // foundation, non-top hole cards, accordion FINAL
    MLD_IN_STOCK   = 2,
    MLD_IN_WASTE   = 3,
    MLD_IN_RESERVE = 4,
    MLD_HOLE_TOP   = 5,   // top card of hole pile (hole games only)
    MLD_IN_SPACE   = 6,   // +k for tableau pile k
};

struct multiplicity_descriptor {
    bool is_predecessor;   // false = locative (MLD_*), true = predecessor (MPD)
    union {
        struct {           // MPD: card sits on predecessor_card_id
            uint8_t predecessor_card_id;  // 0..51
            bool face_down;
        } pred;
        uint8_t locative_kind;  // MLD_* enum value
    };
};
```

This is a separate type from `card_descriptor` — the two coexist.

**`multiplicity_descriptor_store.h`** — the `Policy::descriptor_store_type`:

Holds 52 `multiplicity_descriptor` values plus a 64-byte payload buffer. Interface
mirrors `compact_state` where needed by `generic_flat_cache` but internal representation
is different.

**`multiplicity_zobrist.h`** — new Zobrist table:

`Z[52][80]` for no-symmetry (52 classes x (52 predecessor columns + 28 locative columns)).
Initialised with `mt19937_64`, seed `0xDEADBEEF12345678`, separate from the existing
`zobrist_hash` tables.

### 1.2 — MultiplicityDescriptorEngine

Implements the engine interface from Stage 0 with multiplicity-specific logic:

- **`init()`**: walks game state piles. For each card, determines: is it at pile bottom
  (locative IN_SPACE), sitting on another card (predecessor), in a cell (IN_CELL), on
  foundation (PERMANENT), etc. Uses pile accessors (`piles[ref]`, `top_card()`,
  `is_face_down()`). No dependency on `parent_table` or `card_descriptor` enum.

- **`compute_payload()`**: maps descriptors to 8-bit slot bytes. For no-symmetry, canonical
  position = card ID, so predecessor references trivially resolve. Payload is 52 bytes of
  slot data in a 64-byte entry.

- **`compute_hash()`**: walks descriptors, looks up `multiplicity_zobrist`, applies NOT
  trick for face-down predecessors. For no-symmetry, each static class has one member, so
  additive-within-class degenerates to a single value per class, XOR'd across classes.

- **From-scratch strategy**: for Stages 1–2, each cache probe triggers a full recomputation.
  The engine's move callbacks (`on_card_moved` etc.) just set a dirty flag; payload and hash
  are recomputed lazily. This avoids implementing incremental updates before correctness is
  established.

### 1.3 — MultiplicityPolicy and Cluster Policy

```cpp
struct MultiplicityPolicy {
    static constexpr bool computes_hash        = true;
    static constexpr bool computes_payload     = true;
    static constexpr bool skip_pile_ordering   = true;
    typedef multiplicity_descriptor_store descriptor_store_type;
    typedef generic_flat_cache<MultiplicityClusterPolicy> cache_type;
};
```

`MultiplicityClusterPolicy`: 64-byte payload entries (matching `predecessor_state` size),
`matches()` via `memcmp` over the 52-byte slot range, depth-preferred TwoBig1 replacement
with hash-guard optimisation. The hash guard occupies bytes 56–63 of each entry (8-byte
aligned), overlaying the reserved region of the payload. Before fetching slot 1's payload
(cache line 1), the guard hash is compared against the probe hash; if they differ, the
second cache-line fetch is skipped entirely. This is the same mechanism used by
`PredecessorClusterPolicy` — the `generic_flat_cache` template handles all guard
maintenance via tag dispatch on `insert_predecessor_tag`.

### 1.4 — Dispatch Wiring

Add `use_multiplicity_cache(rules, suit_sym)` to `cache_interface.h`. Initially returns
true only for single-deck, no-symmetry, no-accordion games — same eligibility as
`use_new_cache`. Selection via `--cache-type multiplicity` CLI option so it's opt-in.

Wire into `dispatch_solve()`, `solvability_calc`, and `benchmark` dispatch (3 files with
parallel if/else chains).

### 1.5 — Validation

For each game/seed in the baseline:
- Run with existing flat cache, record (solvability, states searched, hits, misses).
- Run with multiplicity cache, record same.
- **Solvability must match exactly.**
- **States searched, hits, misses must match exactly pre-eviction.** (Post-eviction
  divergence is acceptable due to different payload sizes affecting replacement.)
- Any pre-eviction divergence is a bug.

### 1.6 — Resolved and Open Questions: Eliminated Metadata

The v4 design eliminates foundation top ranks, waste pointer, and hole top from the
payload, claiming they are derivable from the set of cards with PERMANENT descriptor (for
foundations) and the fixed deal sequence (for stock/waste). This must be verified for
foundations and waste; the hole top case is already resolved:

- **Foundation tops:** if the set of PERMANENT cards uniquely determines each foundation's
  top rank, then foundation metadata is redundant. This holds when foundations are built in
  strict rank order (all standard games). Verify across the game library.
- **Waste pointer (RESOLVED):** the v4 design's claim that stock/waste card identity
  alone determines the deal position is incorrect for redeal games. The existing flat
  cache uses a `waste_deal_symmetry` optimisation: when `stock_redeal` is enabled and
  `waste.size() % stock_deal_count == 0`, the stock/waste partition is irrelevant
  (the player can always re-deal to reach the same accessible cards), so the flat cache
  sets `waste_ptr = 0` and gives all stock/waste cards the same descriptor (`STARTING`).
  **Resolution:** the multiplicity engine replicates this: when `waste_deal_symmetry`
  holds, all stock and waste cards receive the same locative descriptor (`MLD_IN_STOCK`),
  collapsing the stock/waste distinction. When the symmetry does NOT hold
  (`waste.size() % stock_deal_count != 0`), cards are distinguished as `MLD_IN_STOCK`
  vs `MLD_IN_WASTE`. This produces exact pre-eviction match with the flat cache on
  Klondike (deal-3, redeal enabled). Verified on 20 seeds: exact match with zero
  evictions; acceptable divergence with evictions (different cluster sizes).
- **Hole top (RESOLVED):** the PERMANENT descriptor is insufficient for hole games. Multiple
  cards in the hole all have PERMANENT, but the *order* matters — the hole top determines
  which cards can be played next. Two states with the same set of hole cards but different
  top cards are distinct game states. **Resolution:** add a `HOLE_TOP` locative to the
  multiplicity descriptor enum (`MLD_HOLE_TOP`), used only for the card currently on top of
  the hole pile. Other hole cards retain `MLD_PERMANENT`. This adds one locative value and
  one Zobrist column per static class but no extra metadata byte, keeping the payload
  self-contained. The `HOLE_TOP` descriptor must be updated on `make_move`/`undo_move`
  when a card is played to or removed from the hole (the old top becomes `MLD_PERMANENT`,
  the new top becomes `MLD_HOLE_TOP`).

If the foundation or waste assumptions fail for a supported game, add the metadata back
to the payload for that game type.

---

## Stage 2: From-Scratch Multiplicity, With Symmetry

**Goal:** Extend Stage 1 to handle suit-symmetry streamliners. Still from-scratch.

**Prerequisite:** Stage 1 complete and validated.

### 2.1 — Static Class Structure

New file `static_class_structure.h`:
- `NONE`: 52 classes of 1 member each (Stage 1 behaviour).
- `COLOUR`: 26 classes of 2 (H<->D, S<->C). Hearts and diamonds share a class per rank;
  spades and clubs share a class per rank.
- `SUIT_IRRELEVANT`: 13 classes of 4 (all suits per rank).

Driven by `sol_rules` + `streamliner_options`. The `MultiplicityDescriptorEngine`
receives the class structure at construction.

### 2.2 — Canonical Position Resolution

The most algorithmically novel component. Within each static class:

1. Compute payload bytes for all members using their descriptors.
2. Sort members by payload byte (ascending). This is the canonical order.
3. Assign canonical positions: class 0's members get positions 0, 1, ...; class 1
   continues; etc.
4. **Fixpoint iteration:** predecessor references point at the canonical position of the
   predecessor card. But canonical positions depend on sort order, which depends on
   predecessor references. Iterate until stable.

The v4 spec's downward-only propagation argument guarantees termination (typically 1–3
iterations). Implement with an iteration cap and assertion.

### 2.3 — Additive Hash Combining

For each static class C:
- Sum (mod 2^64) the Zobrist lookups for all members of C.
- XOR the class sums to produce the final hash.

This replaces pure XOR, which would cancel when two members of a class have the same
descriptor (producing a zero contribution — a hash collision).

### 2.4 — Validation (Asymmetric Criterion)

For symmetric games (colour or suit-irrelevant streamliner active):
- **Old cache hits must be a subset of new cache hits** (up to first eviction). A hit in
  the old cache that becomes a miss in the new cache is a canonicalisation bug.
- **New cache hits not in old cache are valid** — they represent symmetry-detected
  redundancies (two states equivalent under suit permutation now hash identically).
- **Solvability conclusions must match exactly.**

For no-symmetry games: Stage 1 validation still applies (exact match).

### 2.5 — Key Outcome

After Stage 2, the flat cache supports symmetry for the first time. Games that currently
fall back to LRU when `--streamliners suit-symmetry` is active can use the multiplicity
cache instead. This is the primary motivation for the entire effort.

---

## Stage 3: Incremental Computation Specification

**Goal:** Before implementing incremental updates, produce a detailed specification
document covering:

- Exact algorithms for incremental update on `make_move` and `undo_move`.
- Cascade detection: sort reordering (Trigger 1), dynamic class merge (Trigger 2),
  dynamic class split (Trigger 3).
- Worked examples on colour-symmetric Klondike for each trigger type.
- Data structure choices for per-class slot lists, children lists, and Zobrist sums.
- Cost analysis per move type.

**This is a documentation milestone, not a code milestone.** The document must be reviewed
and approved before Stage 4 begins.

---

## Stage 4: Incremental, No Symmetry

**Goal:** Implement incremental descriptor/hash/payload updates for no-symmetry mode.

- The `MultiplicityDescriptorEngine`'s `on_card_moved()` etc. callbacks now perform real
  incremental updates instead of setting a dirty flag.
- Auxiliary data structures maintained: per-class slot lists, per-class Zobrist sums,
  per-card children lists.
- `undo_move` reverts auxiliary structures by replaying the move record in reverse.

**Validation:** Debug-mode assertion on every move: incremental result matches
from-scratch result. Performance benchmark: incremental should be significantly faster
than from-scratch.

---

## Stage 5: Incremental, With Symmetry

**Goal:** Extend incremental updates to handle symmetry, including cascade detection.

- Sort reordering, dynamic class merge/split triggers implemented per Stage 3 spec.
- Cascade depth bounded (max 12 for single-deck).

**Validation:** Debug-mode assertion as in Stage 4, but on symmetric games. Particular
attention to cascade triggers.

---

## Stage 6: Benchmarking and Comparison

**Goal:** Comprehensive performance evaluation.

- **No-symmetry games:** compare multiplicity cache vs existing FlatPolicy. The existing
  policy may be faster (simpler descriptors, smaller payload) — quantify the difference.
- **Symmetric games:** compare multiplicity cache vs LRU. The multiplicity cache should
  be faster (flat cache vs Boost MultiIndex) and provide better deduplication.
- **Hard instances:** memory profiling, cache occupancy, eviction rates.
- **Cascade cost:** if substantial, consider Scheme B (deferred optimisation where children
  of a dynamic class receive distinct labels instead of all pointing to the lowest
  position).

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Stage 0 extraction is larger than expected | Delays all subsequent stages | Stage 0.1 audit scopes the work before committing |
| Foundation/waste/hole metadata not derivable for some game | Payload format must include metadata | Stage 1.6 investigates early; fallback is straightforward |
| Fixpoint iteration in Stage 2 is slow or doesn't converge | Performance, correctness | v4 spec has termination argument; implement iteration cap + assertion |
| Incremental cascades harder than expected | Stages 4–5 blocked | Stage 3 spec document is the mitigation — don't code until spec is reviewed |
| Multiplicity cache slower than FlatPolicy on no-symmetry games | Wasted effort on non-symmetric games | Coexistence: keep both policies, use multiplicity only where symmetry matters |

## Ordering and Dependencies

```
Stage 0.1 (audit)
    |
Stage 0.2 (extract FlatDescriptorEngine)
    |
Stage 0.3 (validate predecessor path)
    |
    +--- Stage 1 (from-scratch, no symmetry)
              |
         Stage 2 (from-scratch, with symmetry)
              |
         Stage 3 (incremental spec document)
              |
              +--- Stage 4 (incremental, no symmetry)
                        |
                   Stage 5 (incremental, with symmetry)
                        |
                   Stage 6 (benchmarking)
```

Each stage has a defined pause point. No stage begins until the previous stage is
reviewed and approved.
