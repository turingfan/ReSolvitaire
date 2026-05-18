# Stage 2 Implementation Plan — Suit-Symmetry Canonicalisation

**Date:** 2026-05-17
**Branch:** multiplicity-encoding
**Prerequisite:** Stage 1 complete and committed (`ffbbd45`)

## Goal

Extend `MultiplicityPolicy` to canonicalise suit-symmetric states.
When `--streamliners suit-symmetry` (or `both`) is active, states that differ
only by a suit permutation produce identical payloads and hashes, enabling the
flat multiplicity cache to replace the LRU cache for symmetric games.

---

## Files to Create

### `src/main/game/multiplicity_static_class.h`

Defines three types:

- `symmetry_mode` enum: `NONE` (52 classes × 1), `COLOUR` (26 × 2), `SUIT_IRRELEVANT` (13 × 4)
- `determine_symmetry_mode(const sol_rules&, bool suit_sym) → symmetry_mode`
  - `!suit_sym` → NONE
  - `rules.hole` → SUIT_IRRELEVANT
  - `build_pol == RED_BLACK` → COLOUR
  - `build_pol == SAME_SUIT` → NONE (suits can't be permuted)
  - otherwise → SUIT_IRRELEVANT
- `static_class_structure` — holds `n_classes`, `class_size`, and three arrays:
  `class_of[52]`, `class_start[52]`, `class_members[52]`; populated by `init(mode)`

**Circular-include note:** The spec prompt referenced `game_state.h` for
`streamliner_options`. This creates a cycle:
`multiplicity_static_class.h` → `game_state.h` → `multiplicity_descriptor_engine.h`
→ `multiplicity_static_class.h`. Resolution: use `bool suit_sym` as parameter;
computed in `make_desc_ctx()` from `stream_opts`.

Card encoding (Solvitaire convention):
- Suit 0 = Clubs (black), 1 = Hearts (red), 2 = Spades (black), 3 = Diamonds (red)
- card_id = suit × 13 + (rank − 1)
- COLOUR class_id = rank_idx × 2 + colour (colour 0 = black suits 0,2; colour 1 = red suits 1,3)
- SUIT_IRRELEVANT class_id = rank_idx (0..12)

---

## Files to Modify

### `src/main/game/flat_descriptor_engine.h`

Add to `descriptor_context`:
```cpp
bool suit_sym = false;   // used by multiplicity_descriptor_engine
```
The flat engine ignores this field; it's present only so multiplicity can read it.

### `src/main/game/search-state/game_state.cpp`

Update `make_desc_ctx()` to set `suit_sym`:
```cpp
ctx.suit_sym = (stream_opts == streamliner_options::SUIT_SYMMETRY
                || stream_opts == streamliner_options::BOTH);
```
(Set on the ctx object after the brace-initialiser.)

### `src/main/game/multiplicity_descriptor_engine.h`

New members:
```cpp
static_class_structure classes;   // initialized once in init()
uint8_t canonical_pos[52];        // working array: card → current canonical position
uint8_t slot[52];                 // working array: card → current slot byte
```

`init(ctx, waste_ptr)` now calls:
```cpp
classes.init(determine_symmetry_mode(ctx.rules, ctx.suit_sym));
```
before `recompute_all(ctx)`.

`recompute_from_descriptors()` is replaced with the 5-phase algorithm below.
`zob_lookup()` now passes `classes.class_of[card_id]` as the table row (not `card_id`).
For NONE mode this is identical to Stage 1 (`class_of[c] = c`).

### `src/main/game/cache_interface.h`

Remove the `suit_symmetry_active` guard from `use_multiplicity_cache`:
```cpp
inline bool use_multiplicity_cache(const sol_rules& rules) {
    return !rules.two_decks
        && rules.sequence_count == 0
        && rules.accordion_size == 0
        && (rules.stock_size == 0
            || rules.stock_deal_t != sol_rules::stock_deal_type::TABLEAU_PILES);
}
```
(The `suit_symmetry_active` parameter can be kept with `/*unused*/` annotation for
callsite compatibility, or callers updated — to be decided at implementation time.)

### `src/main/main.cpp`, `src/main/evaluation/solvability_calc.cpp`, `src/main/evaluation/benchmark.cpp`

In the `auto`-dispatch block, insert a new branch **before** `use_new_cache`:
```cpp
} else if (suit_sym && use_multiplicity_cache(rules)) {
    return solve_game_impl<MultiplicityPolicy>(...);
} else if (use_new_cache(rules, suit_sym)) {       // non-symmetric eligible → FlatPolicy
    return solve_game_impl<FlatPolicy>(...);
} else {
    return solve_game_impl<LRUPolicy>(...);         // symmetric ineligible → LRU
}
```
`benchmark.cpp` has two dispatch functions (seed-based and deal-based); both need updating.

---

## Algorithm: `recompute_from_descriptors()` — 5 phases

Called after `recompute_all()` assigns raw descriptors to all 52 cards.

### Fast path — NONE mode

When `classes.n_classes == 52` (Stage 1 behaviour), canonical_pos[c] = c and the
loop degenerates to Stage 1's XOR-based hash and direct slot bytes. Add an early
return to avoid any overhead.

### Phase 0 — Initialise canonical positions

```
for each class cls (0..n_classes-1):
    for i = 0..class_size-1:
        canonical_pos[class_members[class_start[cls] + i]] = class_start[cls] + i
```

### Phase 1 — Initial slot bytes

```
for each card c (0..51):
    if descriptors[c].is_predecessor:
        pos = canonical_pos[descriptors[c].predecessor_card_id]
        slot[c] = face_down ? (255 - pos) : pos
    else:
        slot[c] = 52 + descriptors[c].locative_kind
```

### Phase 2 — Fixpoint iteration (max 12 iterations)

```
repeat:
    for each class cls:
        sort class_members[class_start[cls]..+class_size] by (slot[member], card_id)
        for i = 0..class_size-1:
            canonical_pos[member[i]] = class_start[cls] + i
    changed = false
    for each card c with is_predecessor:
        new_s = canonical_pos[predecessor_card_id], adjusted for face_down
        if new_s != slot[c]: slot[c] = new_s; changed = true
until !changed
assert(iterations < 12)
```

Sort comparison: ascending slot byte, card_id as tiebreak. With at most 4 elements,
insertion sort suffices.

Note: `class_members` is sorted in-place. Starting order is arbitrary; the fixpoint
converges regardless. `class_of[m]` is never modified.

### Phase 3 — Scheme A predecessor collapsing

```
for each class cls:
    members are in slot-sorted order from Phase 2
    for each maximal group of equal-slot members [i..j-1]:
        lowest_pos = canonical_pos[members[i]]
        if j > i+1:    // multi-member dynamic class
            for each card c with is_predecessor:
                if predecessors[c].predecessor_card_id is any members[i..j-1]:
                    slot[c] = face_down ? (255 - lowest_pos) : lowest_pos
```

After Phase 3, do one final sort+assign (same as Phase 2 sort/assign, once):
```
for each class cls:
    sort and re-assign canonical_pos
```

**Correctness note:** Phase 3 can create new ties within a static class (e.g. 6H and
6D both get slot = position of their shared dynamic predecessor class). These ties
do not need recursive Phase 3 passes. The invariant is that the SORTED MULTISET of
slot bytes within each class is the same for symmetric states; additive Zobrist
combining handles the hash symmetry automatically.

### Phase 4 — Write payload

```
store.clear()
for each class cls:
    for i = 0..class_size-1:
        store.set_slot(class_start[cls] + i, slot[members[i]])
```

`set_slot(pos, byte)` writes `byte` to `data[3 + pos]`. In Stage 2 the argument
`pos` is a canonical position (0..51), not a card_id — the naming in
`multiplicity_descriptor_store.h` is misleading but the operation is correct.

### Phase 5 — Additive hash

```
hash = 0
for each class cls:
    class_sum = 0  (uint64_t wrapping addition)
    for each member m in cls:
        col = is_predecessor ? canonical_pos[predecessor_card_id] : 52 + locative_kind
        z = Z[cls][col]
        class_sum += (face_down ? ~z : z)
    hash ^= class_sum
```

Table row is `cls` (static class ID), NOT card_id. For NONE mode `cls = card_id`,
so Stage 1 behaviour is preserved.

---

## Testing Requirements

All 3 test gates must pass: `python3 scripts/run_tests.py`.

**No-symmetry regression** (Stage 1 criterion):
Run seeds 1–20 with `--cache-type multiplicity` (no streamliner). Must produce
identical `states_searched` to `--cache-type auto` (pre-eviction).

**Symmetric validation:**
```
--type klondike --random <seed> --streamliners suit-symmetry --cache-type multiplicity
```
Compare solvability against `--cache-type auto` (which uses LRU) for seeds 1–20.
All must agree. If any 0-eviction seed shows multiplicity MISS where LRU had HIT,
that is a canonicalisation bug — stop and report.

---

## What NOT to Change

- `recompute_all()` — pile-walking logic unchanged
- `multiplicity_descriptor.h`, `multiplicity_descriptor_store.h` — formats unchanged
- `multiplicity_zobrist.h/cpp` — table dimensions and seed unchanged
- All explicit instantiation blocks — MultiplicityPolicy already present
- `generic_flat_cache_policies.h` — cluster policy unchanged
- Waste-deal symmetry — already correct from Stage 1
