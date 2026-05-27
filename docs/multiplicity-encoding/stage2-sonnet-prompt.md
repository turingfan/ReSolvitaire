# Stage 2 Implementation Prompt — Suit-Symmetry Canonicalisation

You are implementing Stage 2 of the multiplicity encoding for ReSolvitaire. Stage 1
(already committed and validated) provides a from-scratch multiplicity hash with no
symmetry. Stage 2 adds suit-symmetry canonicalisation so the flat cache can deduplicate
states that differ only by suit permutation.

Read CLAUDE.md for build/test commands and `01-Knowledge-Base/AGENTS.md` for mandatory
process rules. All 3 test gates must pass before committing.

---

## What You Are Building

The multiplicity cache currently treats each of 52 cards as its own equivalence class
(class_id = card_id). Stage 2 groups cards into *static classes* by symmetry:

| Mode | Classes | Members/class | When |
|---|---|---|---|
| NONE | 52 | 1 | No symmetry (Stage 1 behaviour) |
| COLOUR | 26 | 2 | `build_pol == RED_BLACK` (Klondike) |
| SUIT_IRRELEVANT | 13 | 4 | `rules.hole` or `build_pol != SAME_SUIT && != RED_BLACK` |

Cards in the same static class are interchangeable under the active symmetry. The cache
must produce identical payloads and hashes for states that differ only by permuting cards
within a static class.

---

## Algorithm: Canonical Resolution (fixpoint)

After `recompute_all()` assigns raw descriptors to all 52 cards, the new
`recompute_from_descriptors()` must:

### Phase 1: Compute raw slot bytes

For each card c, compute a *preliminary* slot byte from its descriptor. For locative
descriptors this is final (`52 + kind`). For predecessor descriptors, use the predecessor's
*current* canonical position (initially: position within the class before resolution).

### Phase 2: Fixpoint iteration

```
repeat (max 12 iterations):
    changed = false
    for each static class C (in class_id order):
        // Sort members of C by their current slot byte (ascending)
        // Ties broken by card_id for determinism
        sort C.members by slot[member], then card_id
        
        // Assign canonical positions
        for i = 0 to C.size - 1:
            canonical_pos[C.members[i]] = C.start + i
        
    // Recompute slot bytes for all predecessor descriptors
    for each card c with descriptor predecessor(q, fd):
        pos = canonical_pos[q]  // q's NEW canonical position
        new_slot = fd ? (255 - pos) : pos
        if new_slot != slot[c]:
            slot[c] = new_slot
            changed = true

until !changed
assert(googled iterations < 12)  // guaranteed by downward-only propagation
```

### Phase 3: Predecessor resolution (Scheme A)

After fixpoint converges, apply dynamic class collapsing: within each static class,
members with *equal* slot bytes form a dynamic class. All cards whose predecessor is in
a dynamic class D resolve to D's *lowest* canonical position.

```
for each static class C:
    // Members are already sorted by slot byte from Phase 2
    // Group consecutive equal slot bytes
    for each group of equal-slot members [start..end]:
        lowest_pos = canonical_pos[members[start]]  // first in group = lowest
        // Any card c whose predecessor is ANY member of this group
        // should use lowest_pos in its slot byte
        for each card c whose descriptor is predecessor(q, fd) where q in group:
            slot[c] = fd ? (255 - lowest_pos) : lowest_pos
```

Then re-sort and assign final canonical positions one more time (single pass — Scheme A
resolution cannot create new ties that weren't already ties).

### Phase 4: Write payload

```
for each static class C:
    sort C.members by slot[member] (ascending), card_id (tiebreak)
    for i = 0 to C.size - 1:
        store.set_slot(C.start + i, slot[C.members[i]])
```

### Phase 5: Additive hash combining

```
hash_value = 0
for each static class C:
    class_sum = 0  (uint64_t, wrapping addition)
    for each member m in C:
        class_sum += zob_lookup(C.class_id, descriptors[m])
    hash_value ^= class_sum
```

The Zobrist lookup uses `Z[class_id][column]` (NOT `Z[card_id][column]` as in Stage 1).

---

## Files to Create

### `src/main/game/multiplicity_static_class.h`

```cpp
#ifndef SOLVITAIRE_MULTIPLICITY_STATIC_CLASS_H
#define SOLVITAIRE_MULTIPLICITY_STATIC_CLASS_H

#include <cstdint>
#include <array>
#include "sol_rules.h"
#include "game_state.h"  // for streamliner_options

// Static class modes
enum class symmetry_mode : uint8_t { NONE, COLOUR, SUIT_IRRELEVANT };

// Determine symmetry mode from rules + streamliner options
// Mirrors the logic in global_cache.cpp:119
inline symmetry_mode determine_symmetry_mode(
    const sol_rules& rules,
    game_state::streamliner_options stream_opts)
{
    bool suit_sym = (stream_opts == game_state::streamliner_options::SUIT_SYMMETRY
                  || stream_opts == game_state::streamliner_options::BOTH);
    if (!suit_sym) return symmetry_mode::NONE;
    if (rules.hole) return symmetry_mode::SUIT_IRRELEVANT;
    if (rules.build_pol == sol_rules::build_policy::RED_BLACK)
        return symmetry_mode::COLOUR;
    if (rules.build_pol == sol_rules::build_policy::SAME_SUIT)
        return symmetry_mode::NONE;  // can't canonicalise
    return symmetry_mode::SUIT_IRRELEVANT;
}

// Static class structure for a given symmetry mode
struct static_class_structure {
    uint8_t n_classes;           // 13, 26, or 52
    uint8_t class_size;          // 4, 2, or 1
    uint8_t class_of[52];        // card_id → class_id
    uint8_t class_start[52];     // class_id → first payload position
    // Members of each class, stored contiguously
    // class_members[class_start[c] .. class_start[c] + class_size - 1]
    uint8_t class_members[52];   // sorted by card_id within each class

    void init(symmetry_mode mode) {
        switch (mode) {
        case symmetry_mode::NONE:
            n_classes = 52; class_size = 1;
            for (uint8_t i = 0; i < 52; i++) {
                class_of[i] = i;
                class_start[i] = i;
                class_members[i] = i;
            }
            break;
        case symmetry_mode::COLOUR:
            // card_id = suit*13 + (rank-1)
            // Suits 0,2 = black (colour 0); suits 1,3 = red (colour 1)
            // class_id = (rank-1)*2 + colour = 0..25
            n_classes = 26; class_size = 2;
            for (uint8_t cid = 0; cid < 52; cid++) {
                uint8_t suit = cid / 13;
                uint8_t rank_idx = cid % 13;  // 0-based rank
                uint8_t colour = (suit == 0 || suit == 2) ? 0 : 1;
                class_of[cid] = rank_idx * 2 + colour;
            }
            for (uint8_t c = 0; c < 26; c++) class_start[c] = c * 2;
            // Fill class_members: for each class, list its 2 members sorted
            // COLOUR class c = rank_idx*2 + colour:
            //   colour 0 (black): suits 0 and 2 → card_ids rank_idx, 26+rank_idx
            //   colour 1 (red):   suits 1 and 3 → card_ids 13+rank_idx, 39+rank_idx
            for (uint8_t c = 0; c < 26; c++) {
                uint8_t rank_idx = c / 2;
                uint8_t colour = c % 2;
                if (colour == 0) {
                    class_members[c * 2]     = rank_idx;       // Clubs
                    class_members[c * 2 + 1] = 26 + rank_idx;  // Spades
                } else {
                    class_members[c * 2]     = 13 + rank_idx;  // Hearts
                    class_members[c * 2 + 1] = 39 + rank_idx;  // Diamonds
                }
            }
            break;
        case symmetry_mode::SUIT_IRRELEVANT:
            // class_id = rank-1 = 0..12
            n_classes = 13; class_size = 4;
            for (uint8_t cid = 0; cid < 52; cid++) {
                class_of[cid] = cid % 13;  // rank_idx
            }
            for (uint8_t c = 0; c < 13; c++) class_start[c] = c * 4;
            // Members: suits 0,1,2,3 for each rank
            for (uint8_t c = 0; c < 13; c++) {
                for (uint8_t s = 0; s < 4; s++) {
                    class_members[c * 4 + s] = s * 13 + c;
                }
            }
            break;
        }
    }
};

#endif
```

---

## Files to Modify

### `src/main/game/multiplicity_descriptor_engine.h`

Major changes:
1. Add `#include "multiplicity_static_class.h"`
2. Add member: `static_class_structure classes;`
3. Add member: `uint8_t canonical_pos[52];`  (computed during fixpoint)
4. Add member: `uint8_t slot[52];`  (working array for slot bytes)
5. Constructor: accept `symmetry_mode` and call `classes.init(mode)`
6. Replace `canonical_position(card_id)` with `canonical_pos[card_id]` lookup
7. Replace `recompute_from_descriptors()` with the fixpoint algorithm above
8. Update `zob_lookup()`: use `classes.class_of[card_id]` as table row instead of `card_id`

The `recompute_all(ctx)` method is unchanged (it still walks piles and assigns raw
descriptors). Only `recompute_from_descriptors()` changes.

**Critical:** When `classes.class_size == 1` (NONE mode), the fixpoint converges in
exactly one pass with `canonical_pos[c] = c` (identical to Stage 1). Add an early-return
fast path for this case to avoid any performance regression on non-symmetric games.

### `src/main/game/multiplicity_zobrist.h`

No structural change needed. The table is already Z[52][80]. Under COLOUR mode, only rows
0–25 are used. Under SUIT_IRRELEVANT, only rows 0–12. The init function fills all 52 rows
regardless (deterministic seed means the values are stable). The engine just indexes with
`class_id` instead of `card_id`.

### `src/main/game/flat_descriptor_engine.h`

Add to `descriptor_context`:
```cpp
game_state::streamliner_options stream_opts = game_state::streamliner_options::NONE;
```

### `src/main/game/search-state/game_state.h` and `game_state.cpp`

Update `make_desc_ctx()` to include `stream_opts`:
```cpp
ctx.stream_opts = stream_opts;
```

### `src/main/game/cache_interface.h`

Change `use_multiplicity_cache()` to remove the suit-symmetry guard:
```cpp
inline bool use_multiplicity_cache(const sol_rules& rules, bool /*suit_symmetry_active*/ = false) {
    return !rules.two_decks
        && rules.sequence_count == 0
        && rules.accordion_size == 0
        && (rules.stock_size == 0 || rules.stock_deal_t != sol_rules::stock_deal_type::TABLEAU_PILES);
}
```

### `src/main/main.cpp`

Update the dispatch logic so that when `--cache-type auto` and suit symmetry is active,
multiplicity-eligible games use `MultiplicityPolicy` instead of falling through to LRU.

The key change is in the auto-dispatch block: if `use_multiplicity_cache(rules, false)` is
true (game is structurally eligible), use MultiplicityPolicy regardless of `suit_sym`.

---

## What NOT to Change

- `recompute_all()` — the pile-walking logic is unchanged
- Waste-deal symmetry — already correct from Stage 1
- `multiplicity_descriptor.h` — descriptor structure unchanged
- `multiplicity_descriptor_store.h` — payload format unchanged (still 52 slot bytes)
- All explicit instantiation blocks — already have MultiplicityPolicy
- The 128-byte cluster policy — unchanged

---

## Testing Requirements

1. **All 3 test gates must pass** (`python3 scripts/run_tests.py`)
2. **No-symmetry regression:** Run seeds 1–20 with `--cache-type multiplicity` (no
   streamliner). Must produce identical `states_searched` to `--cache-type auto` on all
   seeds with 0 evictions (same as Stage 1 criterion).
3. **Symmetric validation:** Run seeds 1–20 with:
   ```
   --type klondike --streamliners suit-symmetry --cache-type multiplicity
   ```
   Compare solvability against `--cache-type auto` (LRU). All must agree.
4. If any 0-eviction symmetric seed shows a multiplicity MISS where LRU had a HIT,
   that is a canonicalisation bug — stop and report.

---

## Constraints

- Do NOT modify any file outside the multiplicity-encoding scope
- Do NOT implement incremental updates (that's Stage 4)
- Do NOT change the Zobrist seed or table dimensions
- Do NOT touch PredecessorPolicy or FlatPolicy
- Keep the no-symmetry fast path (class_size == 1 → Stage 1 behaviour exactly)
- Maximum fixpoint iterations: 12 (assert on overflow)
- The constructor must determine symmetry_mode from rules + stream_opts, not from a new CLI flag
