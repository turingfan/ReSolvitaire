# Stage 0.1: Descriptor Interface Audit

**Date:** 15 May 2026
**Branch:** `multiplicity-encoding`
**Reference:** `01-Knowledge-Base/Implementation-Plans/MultiplicityEncoding-DetailedPlan-20260513.md`

## Purpose

Catalogue every location in `game_state.cpp` where descriptor, hash, or payload logic
is hardcoded behind `if constexpr (Policy::computes_hash)` or `computes_payload` guards.
The goal is to determine the interface that a "descriptor engine" must provide so that a
new multiplicity policy can plug in without forking move logic.

## Audit Methodology

Searched all 24 occurrences of `if constexpr (Policy::computes_hash)` and
`if constexpr (Policy::computes_payload)` in `game_state.cpp` (on `dev` at commit
`f3258c0`). Each is classified below by the operation it performs.

---

## Operation Catalogue

### Op 1: Init — `init_payload_and_hash()` (lines 1152–1243)

**Called from:** all three game_state constructors (JSON doc, seed, initializer_list).

**What it does:**
1. Clears `desc_store`, sets `zobrist_hash_value = 0`.
2. XORs in `Z_card[c][STARTING]` for all 52 cards (baseline hash).
3. Sets foundation top ranks in `desc_store`, XORs foundation keys into hash.
4. Sets hole top in `desc_store`, XORs hole key into hash.
5. Sets waste pointer in `desc_store`, XORs waste key into hash.
6. Walks all face-up tableau cards, assigns descriptors:
   - Bottom of pile -> `IN_SPACE`
   - Face-down parent below -> leave as `STARTING` (skip)
   - Face-up parent below -> `parent_table::get_descriptor_for_parent()` -> `PARENT_0`..`PARENT_3` or `ROOT`
7. Walks pre-filled cells -> `IN_CELL`.
8. Walks hole cards -> `IN_HOLE`.

**Dependencies on current system:** `card_descriptor` enum, `parent_table`, `zobrist_hash::card_key/foundation_key/hole_top_key/waste_key`.

### Op 2: Init fixup — `init_initially_face_up()` (lines 1121–1149)

**Called from:** constructors, after `init_payload_and_hash()`.

**What it does:**
1. Records which cards are face-up after initial deal (for STARTING vs STARTING_FACE_UP disambiguation on undo).
2. Fixes single-card tableau piles: if top card is face-up but has STARTING descriptor (because init ran before `turn_face_up()`), changes it to IN_SPACE.

**Dependencies:** `desc_store.get_descriptor()`, `card_descriptor::STARTING`, `card_descriptor::IN_SPACE`.

### Op 3: Determine destination — `determine_destination_descriptor()` (lines 1310–1360)

**Called from:** `make_regular_move`, `make_stock_k_plus_move`, `make_stock_to_all_tableau_move`.

**What it does:** Given a destination pile and moved card, returns the new descriptor:
- Foundation -> `STARTING` (reused as "on foundation")
- Hole -> `IN_HOLE`
- Cell -> `IN_CELL`
- Tableau, pile was empty -> `IN_SPACE`
- Tableau, has face-up parent below -> `parent_table::get_descriptor_for_parent()` or `ROOT`
- Reserve/stock/waste -> `STARTING`

**Dependencies:** `card_descriptor` enum, `parent_table`, pile accessors.

### Op 4: Card descriptor update — `update_card_descriptor()` (lines 1246–1255)

**Called from:** every make/undo move function.

**What it does:**
1. Reads old descriptor from `desc_store`.
2. Writes new descriptor to `desc_store`.
3. XORs out old Zobrist contribution, XORs in new: `hash ^= Z[cid][old] ^ Z[cid][new]`.

**This is the core incremental hash update primitive.** All move functions ultimately
express their hash changes through this plus the metadata update functions.

### Op 5: Foundation update — `update_foundation_in_hash()` (lines 1257–1267)

**What it does:** XOR-delta for foundation top rank change.

**Multiplicity note:** v4 eliminates foundation metadata. This operation would be
removed or become a no-op if foundation tops are confirmed derivable.

### Op 6: Waste pointer update — `update_waste_ptr_in_hash()` (lines 1280–1289)

**What it does:** XOR-delta for waste pointer change.

**Multiplicity note:** v4 eliminates waste pointer. Same as Op 5.

### Op 7: Hole top update — `update_hole_top_in_hash()` (lines 1291–1301)

**What it does:** XOR-delta for hole top card ID change.

**Multiplicity note:** v4 replaces this with the `MLK_HOLE_TOP` descriptor. The separate
metadata field is eliminated; instead the engine updates the top card's descriptor from
`MLK_PERMANENT` to `MLK_HOLE_TOP` (or vice versa).

### Op 8: Hash recomputation — `compute_hash_from_scratch()` (lines 1370–1395)

**Called from:** testing code.

**What it does:** Walks `desc_store`, XORs all card/foundation/hole/waste keys. Used to
verify incremental hash against from-scratch.

### Op 9: Payload depth — `set_payload_depth()` (line 1364)

**What it does:** Sets depth field in `desc_store`. Called by solver before cache insert.

---

## Move Function Patterns

Each move type follows the same pattern:

### make_regular_move (lines 470–535)
1. Capture pre-move state (moved card ID, foundation suits, hole top).
2. **Pile operations:** `place_card(to, take_card(from))`.
3. **Op 3+4:** `determine_destination_descriptor(to, moved)` -> `update_card_descriptor(cid, new_desc)`.
4. **Op 5:** Update foundation if from/to is foundation.
5. **Op 7:** Update hole top if to is hole.
6. **Op 6:** Update waste ptr if from is waste.
7. **Reveal handling:** if `m.reveal_move`, turn card face-up, then Op 4: set to `IN_SPACE` (if now at pile bottom) or `STARTING_FACE_UP`.

### undo_regular_move (lines 538–616)
1. Identify moved card at destination.
2. **Pile operations:** undo reveal (turn face-down), then `place_card(from, take_card(to))`.
3. **Op 4:** Revealed card -> `STARTING`.
4. **Op 5:** Undo foundation changes.
5. **Op 7:** Undo hole top.
6. **Op 6:** Undo waste ptr.
7. **Op 4:** Moved card -> old descriptor. Old descriptor computation:
   - If source pile now has only the returned card -> `IN_SPACE`
   - If card below is face-down -> `STARTING` or `STARTING_FACE_UP` (via `initially_face_up[]`)
   - If card below is face-up -> `parent_table::get_descriptor_for_parent()` or `ROOT`

### make_built_group_move (lines 605–670)
Same as regular but operates on `m.count` cards. Only the **bottom card** of the group
changes descriptor (the internal cards' descriptors don't change — they still sit on each
other). Uses Op 3 equivalent logic inline (not calling `determine_destination_descriptor`).

### undo_built_group_move (lines 673–747)
Same pattern. Recovers bottom card's old descriptor with same logic as undo_regular_move.

### make_stock_k_plus_move (lines 750–811)
1. Deal `m.count` cards from stock to waste (or reverse for redeal).
2. Play top of waste to `m.to`.
3. **Op 3+4:** Played card gets destination descriptor.
4. **Op 5, 7, 6:** Foundation, hole, waste updates.

### undo_stock_k_plus_move (lines 813–878)
Reverse. Played card -> `STARTING` (back to stock/waste).

### make_stock_to_all_tableau_move (lines 880–898)
Deals from stock to each tableau pile. For each dealt card: Op 3+4.
**Note:** Asserts `!use_new_cache(rules)` — always LRU. But the `if constexpr` block
still compiles for all policies.

### undo_stock_to_all_tableau_move (lines 900–915)
Each dealt card -> `STARTING`.

### make_sequence_move / undo_sequence_move
No hash updates. Sequence games don't use the Zobrist cache.

### make_accordion_move / undo_accordion_move
Uses the separate predecessor Zobrist system (`Z_pred`, `predecessor_array`,
`update_predecessor()`). Does NOT use Op 3/4/5/6/7. Completely independent path.

---

## Summary: Required Engine Interface

Based on the audit, a descriptor engine must provide:

```
// Lifecycle
init(game_state)                    — Op 1+2: from-scratch initialisation
get_hash() -> uint64_t              — current hash value
get_payload() -> payload_type&      — current payload for cache comparison
set_depth(uint16_t)                 — Op 9: set depth in payload
recompute_hash()                    — Op 8: verify hash from payload

// Per-card descriptor
determine_descriptor(dest_pile, moved_card, game_state) -> descriptor
                                    — Op 3: what descriptor should a card get?
update_card(cid, new_descriptor)    — Op 4: update descriptor + hash

// Metadata (current system; multiplicity engine may eliminate some)
update_foundation(suit, new_rank)   — Op 5
update_waste_ptr(new_ptr)           — Op 6
update_hole_top(new_cid)            — Op 7

// Face-down handling
init_face_up_table(game_state)      — Op 2: record initial face-up state
get_undo_descriptor(cid, source_pile, card_below, game_state) -> descriptor
                                    — used by undo functions to recover old descriptor
```

### Observations

1. **Move functions don't need to change.** The pattern in every make/undo function is:
   pile operations, then call engine methods. If the engine interface is right, the
   move functions can call through it without knowing which descriptor system is in use.

2. **`initially_face_up[]` is descriptor-system-specific.** The current system needs it
   to disambiguate STARTING vs STARTING_FACE_UP on undo. The multiplicity system would
   handle this differently (face-down is encoded in the predecessor descriptor's `fd`
   flag, not a separate enum value). This array should move into the engine.

3. **`determine_destination_descriptor` is the key policy point.** This function embodies
   the descriptor philosophy — it decides what a card's state means. Everything else is
   plumbing around it.

4. **Metadata operations (Op 5/6/7) may become no-ops.** The multiplicity engine
   encodes foundation/waste/hole state within the per-card descriptors, so separate
   metadata updates may not be needed. The interface should still have these methods
   (for the existing engine) but the multiplicity engine can implement them as no-ops.

5. **The accordion/predecessor path is already fully separate.** It has its own hash
   (`predecessor_zobrist_hash`), its own payload (`pred_payload`), its own update
   function (`update_predecessor`). It never touches `desc_store` or `zobrist_hash_value`.
   Stage 0 extraction doesn't need to touch it.

---

## Recommendation: Engine Class vs Static Methods

**Recommendation: engine class** (not static methods on Policy).

Reasons:
- The engine has mutable state: `desc_store`, `zobrist_hash_value`, `initially_face_up[]`.
  Currently these are members of `game_state_impl`. An engine class would own them.
- The multiplicity engine will have additional state: per-class slot lists, per-class
  Zobrist sums, children lists. These don't belong on `game_state_impl`.
- The engine needs access to the game state (pile structure, rules) to compute
  descriptors. It should hold a reference/pointer to the game state, not be a static
  utility.

**Proposed structure:**

```cpp
// In cache_policy.h or a new header:
template <typename Policy>
using descriptor_engine_t = typename Policy::descriptor_engine;

// FlatPolicy would define:
struct FlatPolicy {
    // ... existing fields ...
    using descriptor_engine = flat_descriptor_engine;
};

// game_state_impl would have:
typename Policy::descriptor_engine desc_engine;
// instead of separate desc_store, zobrist_hash_value, initially_face_up
```

The engine class wraps `desc_store` + `zobrist_hash_value` + `initially_face_up` +
all the update methods. Move functions call `desc_engine.update_card(...)` etc.

---

## Next Step: Stage 0.2

Extract the current logic into `flat_descriptor_engine`. The audit above provides the
complete list of what moves into the engine. The key files to modify:

- **New:** `src/main/game/flat_descriptor_engine.h` — the engine class
- **Modified:** `game_state.h` — replace `desc_store` + hash + face-up array with engine
- **Modified:** `game_state.cpp` — replace inline `if constexpr` logic with engine calls
- **Modified:** `cache_policy.h` — add `descriptor_engine` typedef to FlatPolicy, HashOnlyPolicy

Accordion/predecessor path is **not touched** in Stage 0.2 (it's already separate).
