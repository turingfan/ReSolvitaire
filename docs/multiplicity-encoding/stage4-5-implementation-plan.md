# Stage 4-5 Implementation Plan: Incremental Multiplicity Updates

**Date:** 2026-05-20
**Status:** Draft — pending review of `stage3-incremental-update-proposal.md`
**Authors:** Ian Gent & AI Assistant (Claude Opus 4.6)
**Prerequisite:** Stage 3 proposal approved, Stages 0-2 complete and validated
**Audience:** Implementation agent (Sonnet-class)

## Reference Documents

- `docs/multiplicity-encoding/stage3-incremental-update-proposal.md` — algorithm spec
- `docs/multiplicity-encoding/implementation-plan.md` — master plan (Stages 0-6)
- `01-Knowledge-Base/Design-Documents/multiplicity_encoding_v5.tex` — v5.1 spec
- `CLAUDE.md` — build commands, all three test gates, architecture

## Overview

This plan implements the incremental update algorithm specified in the Stage 3
proposal. The work is split into two stages:

- **Stage 4** — Incremental updates for NONE mode (no symmetry, no cascade)
- **Stage 5** — Incremental updates for COLOUR and SUIT_IRRELEVANT modes (cascade)

Each stage has sub-tasks ordered by dependency. Every sub-task ends with a
validation step. **All three test gates must pass after every sub-task.**

The from-scratch `recompute_all()` is retained permanently as the debug-mode
reference oracle. It is never deleted.

---

## Stage 4: Incremental, No Symmetry

### 4.0 — Add Auxiliary Data Structures to the Engine

**Goal:** Add `children[]`, `class_sum[]`, and scratch arrays to
`multiplicity_descriptor_engine`. Initialise them in `init()`. No behaviour
change yet — `recompute_all()` still runs on every move.

**File:** `src/main/game/multiplicity_descriptor_engine.h`

**Add these data members** (after the existing `slot[52]` array):

```cpp
// ── Auxiliary data for incremental updates ──────────────────────────────
int8_t   children[52];       // children[q] = card sitting on q, or -1
uint64_t class_sum[52];      // per-static-class Zobrist sum (indexed by class_id)
                             // only [0..n_classes-1] used

// ── Scratch arrays (only meaningful during incremental_update) ──────────
uint8_t  old_slot_save[52];  // saved pre-update slot bytes for changed cards
uint64_t changed_mask;       // bitmask of cards whose slot bytes changed
                             // (bits 0-51; 64-bit is sufficient)
```

**Initialise in the constructor:**

```cpp
multiplicity_descriptor_engine() : hash_value(0), changed_mask(0) {
    // ... existing init ...
    std::memset(children, -1, sizeof(children));
    std::memset(class_sum, 0, sizeof(class_sum));
}
```

**Initialise `children[]` and `class_sum[]` at the end of `recompute_all()`**
(after the existing hash computation). Add a private helper:

```cpp
void rebuild_auxiliary() {
    // Rebuild children[] from descriptors[]
    std::memset(children, -1, sizeof(children));
    for (uint8_t c = 0; c < 52; c++) {
        if (descriptors[c].is_predecessor) {
            children[descriptors[c].predecessor_card_id] = static_cast<int8_t>(c);
        }
    }

    // Rebuild class_sum[] from the just-computed hash
    // (reuses the Phase 5 loop structure but stores per-class)
    if (classes.n_classes == 52) {
        // NONE mode: class_sum[c] = zob_for_card(c, c)
        for (uint8_t c = 0; c < 52; c++) {
            class_sum[c] = zob_for_card(c, c);
        }
    } else {
        for (uint8_t cls = 0; cls < classes.n_classes; cls++) {
            const uint8_t base = classes.class_start[cls];
            uint64_t sum = 0;
            for (uint8_t i = 0; i < classes.class_size; i++) {
                sum += zob_for_card(cls, classes.class_members[base + i]);
            }
            class_sum[cls] = sum;
        }
    }
}
```

**Call `rebuild_auxiliary()` at the end of `recompute_from_descriptors()`**
(after the Phase 5 hash computation).

**Also call `rebuild_auxiliary()` at the end of `recompute_all()`** (the
public method, after `recompute_from_descriptors()` returns). Actually,
`recompute_all()` calls `recompute_from_descriptors()` at line 199, so
putting it there is sufficient.

**Validation:**
1. `./build.sh --release --unit-tests && cd cmake-build-release && ctest -R ^unit_tests$ --output-on-failure`
2. `python3 scripts/run_tests.py --quick`
3. No behaviour change expected — just new data members being initialised.

---

### 4.1 — Implement `compute_new_descriptor()` Helper

**Goal:** Given a card and the current board state (via `descriptor_context`),
compute what the card's multiplicity descriptor should be. This is the
multiplicity equivalent of `determine_destination_descriptor()` from the flat
engine, but returns a `multiplicity_descriptor` instead of a `uint8_t`.

This logic already exists inside `recompute_all()` (lines 99-199 of the
current engine). The task is to extract the per-card logic into a callable
helper.

**File:** `src/main/game/multiplicity_descriptor_engine.h`

**Add a public static method:**

```cpp
// Compute the multiplicity descriptor for card `c` based on its current
// position in the board described by `ctx`.
// Returns the descriptor that recompute_all() would assign to this card.
static multiplicity_descriptor compute_descriptor_for_card(
    uint8_t cid, const descriptor_context& ctx);
```

**Implementation:** Walk through the same location checks as `recompute_all()`
but for a single card. The card's location is determined by searching the
piles in `ctx`. The method needs to:

1. Check foundations — if card is in a foundation pile: `make_locative(MLD_PERMANENT)`
2. Check hole — if at top of hole: `make_locative(MLD_HOLE_TOP)`, else `MLD_PERMANENT`
3. Check cells — if in a cell: `make_locative(MLD_IN_CELL)`
4. Check stock — `make_locative(MLD_IN_STOCK)`
5. Check waste — `make_locative(MLD_IN_WASTE)` or `MLD_IN_STOCK` (waste-deal symmetry)
6. Check reserve — `make_locative(MLD_IN_RESERVE)`
7. Check tableau — bottom of pile: `make_locative(space_kind, face_down)`;
   otherwise: `make_predecessor(parent_cid, face_down)`

**Important:** This is expensive (searches piles). It is called only 1-2 times
per move (for the changed cards), not 52 times. In Stage 4, do NOT optimise
this — correctness first.

**Alternative approach (preferred):** Instead of searching piles, determine
the descriptor from the move itself. The move tells us:
- Which card moved (from `piles[m.to].top_card()` after placement)
- Where it went (`m.to`)
- Whether a card was revealed (`m.reveal_move`)

Add a method that takes a card, its destination pile ref, and the context,
and returns the multiplicity descriptor:

```cpp
multiplicity_descriptor descriptor_for_destination(
    uint8_t cid, pile::ref dest, const descriptor_context& ctx) const;
```

This mirrors `flat_descriptor_engine::determine_destination_descriptor()` but
returns a `multiplicity_descriptor`. The implementation checks:
- Is `dest` a foundation? → `make_locative(MLD_PERMANENT)`
- Is `dest` the hole? → `make_locative(MLD_HOLE_TOP)`
- Is `dest` a cell? → `make_locative(MLD_IN_CELL)`
- Is `dest` a tableau pile? → If pile has one card (just placed):
  `make_locative(space_kind, false)`. Else: `make_predecessor(parent_cid, false)`.
  The parent is `ctx.piles[dest][1]` (card below the just-placed card).

For revealed cards, the descriptor doesn't need destination lookup — the
revealed card stays in place. Its descriptor is the same as before but with
`face_down = false`:

```cpp
multiplicity_descriptor descriptor_after_reveal(
    uint8_t cid, pile::ref pile_ref, const descriptor_context& ctx) const;
```

**Validation:** Write a unit test that calls `compute_descriptor_for_card()`
for all 52 cards in a known game state and asserts the results match what
`recompute_all()` produces in `descriptors[]`. Run on 2-3 game types.

---

### 4.2 — Implement `incremental_update_none()` (NONE Mode Only)

**Goal:** Implement the O(k) fast path for NONE mode. No cascade needed.

**File:** `src/main/game/multiplicity_descriptor_engine.h`

**Add a public method:**

```cpp
// Incremental update for NONE mode.
// `changes` is a small inline array of (card_id, new_descriptor) pairs.
// `n_changes` is the count (typically 1-2).
void incremental_update_none(
    const std::pair<uint8_t, multiplicity_descriptor>* changes,
    uint8_t n_changes);
```

**Implementation (following Stage 3 proposal, Step 0 + Step 2, no Step 1):**

```cpp
void incremental_update_none(
    const std::pair<uint8_t, multiplicity_descriptor>* changes,
    uint8_t n_changes)
{
    assert(classes.n_classes == 52);  // NONE mode only

    for (uint8_t i = 0; i < n_changes; i++) {
        uint8_t c = changes[i].first;
        const auto& new_d = changes[i].second;
        const auto& old_d = descriptors[c];

        // Update children[]
        if (old_d.is_predecessor)
            children[old_d.predecessor_card_id] = -1;
        if (new_d.is_predecessor)
            children[new_d.predecessor_card_id] = static_cast<int8_t>(c);

        // Update descriptor
        descriptors[c] = new_d;

        // Compute new slot byte (NONE: canonical_pos[q] = q always)
        uint8_t new_s = raw_slot(c);

        // Update hash via class_sum delta
        // In NONE mode: class_id = card_id, so class_sum[c] = zob_for_card(c, c)
        hash_value ^= class_sum[c];          // XOR out old
        slot[c] = new_s;
        class_sum[c] = zob_for_card(c, c);   // recompute with new slot
        hash_value ^= class_sum[c];          // XOR in new

        // Update payload
        store.set_slot(c, new_s);            // NONE: canonical_pos = card_id
    }
}
```

**Do NOT wire this into `make_move`/`undo_move` yet.** That happens in 4.3.

**Validation:** Unit test — manually set up descriptors, call
`incremental_update_none()` with a known change, assert hash and payload
match what `recompute_from_descriptors()` would produce.

---

### 4.3 — Wire Incremental Updates into `make_move` / `undo_move`

**Goal:** Replace the `recompute_all()` call in `make_move()` and `undo_move()`
with move-type-specific incremental updates, for NONE mode only.

**File:** `src/main/game/search-state/game_state.cpp`

**Current code (lines 429-431):**

```cpp
if constexpr (Policy::computes_multiplicity_descriptor) {
    desc_engine.recompute_all(make_desc_ctx());
}
```

**Replace with:**

```cpp
if constexpr (Policy::computes_multiplicity_descriptor) {
    if (desc_engine.is_none_mode()) {
        // Move-type-specific incremental update computed above
        // (changes[] populated by the move handler)
        desc_engine.incremental_update_none(mult_changes, mult_n_changes);
    } else {
        desc_engine.recompute_all(make_desc_ctx());  // symmetry: Stage 5
    }
#ifndef NDEBUG
    // Verify: incremental result must match from-scratch
    desc_engine.verify_against_scratch(make_desc_ctx());
#endif
}
```

**For each move type handler** (`make_regular_move`, `undo_regular_move`,
`make_built_group_move`, etc.), add code to compute the `mult_changes` array
**before** the `recompute_all()` / `incremental_update_none()` call. Declare
at the top of `make_move()` and `undo_move()`:

```cpp
std::pair<uint8_t, multiplicity_descriptor> mult_changes[4]; // max 4 changes
uint8_t mult_n_changes = 0;
```

**Per-move-type change identification:**

#### `make_regular_move(m)`

After pile operations (line 498), before the `computes_multiplicity_descriptor`
block:

```cpp
if constexpr (Policy::computes_multiplicity_descriptor) {
    // Moved card: now at top of m.to
    card moved = piles[m.to].top_card();
    uint8_t cid = zobrist_hash::card_id(moved.get_suit(), moved.get_rank());
    mult_changes[mult_n_changes++] = {cid,
        desc_engine.descriptor_for_destination(cid, m.to, make_desc_ctx())};

    // Revealed card (if any): at top of m.from, now face-up
    if (m.reveal_move) {
        card rev = piles[m.from][0];
        uint8_t rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());
        mult_changes[mult_n_changes++] = {rev_cid,
            desc_engine.descriptor_after_reveal(rev_cid, m.from, make_desc_ctx())};
    }
}
```

#### `undo_regular_move(m)`

After pile operations are reversed (card returned to `m.from`):

```cpp
if constexpr (Policy::computes_multiplicity_descriptor) {
    // Card returned to m.from (now at top)
    card moved = piles[m.from].top_card();
    uint8_t cid = zobrist_hash::card_id(moved.get_suit(), moved.get_rank());
    mult_changes[mult_n_changes++] = {cid,
        desc_engine.descriptor_for_destination(cid, m.from, make_desc_ctx())};

    // Revealed card undone: card at piles[m.from][1] turned face-down
    if (m.reveal_move) {
        card rev = piles[m.from][1];
        uint8_t rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());
        // Face-down descriptor: same predecessor/locative but fd=true
        mult_changes[mult_n_changes++] = {rev_cid,
            desc_engine.descriptor_for_position(rev_cid, m.from, 1, make_desc_ctx())};
    }
}
```

#### `make_built_group_move(m)`

Only the bottom card of the group changes descriptor:

```cpp
if constexpr (Policy::computes_multiplicity_descriptor) {
    // Bottom of group: now at piles[m.to][m.count - 1]
    card bottom = piles[m.to][m.count - 1];
    uint8_t bottom_cid = zobrist_hash::card_id(bottom.get_suit(), bottom.get_rank());
    // Parent (if any) is at piles[m.to][m.count]
    multiplicity_descriptor new_d;
    if (static_cast<pile::size_type>(piles[m.to].size()) == m.count) {
        // pile_sym logic for IN_SPACE vs IN_SPACE+k
        uint8_t space_kind = pile_sym_kind(m.to);
        new_d = multiplicity_descriptor::make_locative(space_kind, false);
    } else {
        card parent = piles[m.to][m.count];
        uint8_t parent_cid = zobrist_hash::card_id(parent.get_suit(), parent.get_rank());
        new_d = multiplicity_descriptor::make_predecessor(parent_cid, false);
    }
    mult_changes[mult_n_changes++] = {bottom_cid, new_d};

    if (m.reveal_move) {
        card rev = piles[m.from][0];
        uint8_t rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());
        mult_changes[mult_n_changes++] = {rev_cid,
            desc_engine.descriptor_after_reveal(rev_cid, m.from, make_desc_ctx())};
    }
}
```

#### `make_stock_k_plus_move(m)`

Stock/waste moves only change locative descriptors. For NONE mode this is a
sequence of slot byte updates. The simplest correct approach: collect all
changed card descriptors.

However, `stock_k_plus` moves k cards between stock and waste, then plays one
to a target. In NONE mode, stock/waste cards have locative descriptors
(`MLD_IN_STOCK` / `MLD_IN_WASTE`). The k transfers change descriptors, plus
the played card changes.

**For Stage 4, use `recompute_all()` for stock_k_plus.** The incremental
benefit is small (stock/waste are locatives, O(k) anyway) and the move is
complex. Optimise in Stage 6 if needed.

```cpp
if constexpr (Policy::computes_multiplicity_descriptor) {
    // stock_k_plus: too many changes for inline array; use recompute_all
    desc_engine.recompute_all(make_desc_ctx());
}
```

#### `make_stock_to_all_tableau_move(m)`

Similar: deals to multiple piles. **Use `recompute_all()` for Stage 4.**

#### `make_sequence_move(m)` / `undo_sequence_move(m)`

Sequence games never use the multiplicity cache (`rules.sequence_count == 0`
is required by `use_multiplicity_cache()`). No changes needed.

#### `make_accordion_move(m)` / `undo_accordion_move(m)`

Accordion games use PredecessorPolicy, not MultiplicityPolicy. No changes
needed.

**Add `is_none_mode()` to the engine:**

```cpp
bool is_none_mode() const { return classes.n_classes == 52; }
```

**Add `verify_against_scratch()` for debug-mode validation:**

```cpp
#ifndef NDEBUG
void verify_against_scratch(const descriptor_context& ctx) {
    // Save current state
    uint64_t saved_hash = hash_value;
    multiplicity_descriptor_store saved_store = store;
    uint8_t saved_slot[52];
    std::memcpy(saved_slot, slot, sizeof(slot));
    uint8_t saved_canonical[52];
    std::memcpy(saved_canonical, canonical_pos, sizeof(canonical_pos));

    // Recompute from scratch
    recompute_all(ctx);

    // Compare
    assert(hash_value == saved_hash);
    assert(store.matches(saved_store));
    for (uint8_t c = 0; c < 52; c++) {
        assert(slot[c] == saved_slot[c]);
        assert(canonical_pos[c] == saved_canonical[c]);
    }

    // Restore incremental state (recompute_all overwrites everything)
    hash_value = saved_hash;
    store = saved_store;
    std::memcpy(slot, saved_slot, sizeof(slot));
    std::memcpy(canonical_pos, saved_canonical, sizeof(canonical_pos));
}
#endif
```

**Wait — there is a problem.** `recompute_all()` in `verify_against_scratch`
will overwrite `children[]` and `class_sum[]` (since we added
`rebuild_auxiliary()` to it in 4.0). We need to save and restore those too.
Add:

```cpp
#ifndef NDEBUG
void verify_against_scratch(const descriptor_context& ctx) {
    // Save incremental state
    uint64_t saved_hash = hash_value;
    multiplicity_descriptor_store saved_store = store;
    uint8_t saved_slot[52], saved_canonical[52];
    int8_t saved_children[52];
    uint64_t saved_class_sum[52];
    uint8_t saved_class_members[52];
    std::memcpy(saved_slot, slot, 52);
    std::memcpy(saved_canonical, canonical_pos, 52);
    std::memcpy(saved_children, children, 52);
    std::memcpy(saved_class_sum, class_sum, sizeof(class_sum));
    std::memcpy(saved_class_members, classes.class_members, 52);
    multiplicity_descriptor saved_descriptors[52];
    std::memcpy(saved_descriptors, descriptors, sizeof(descriptors));

    // Recompute from scratch
    recompute_all(ctx);

    // Compare core outputs
    assert(hash_value == saved_hash && "incremental hash mismatch");
    assert(store.matches(saved_store) && "incremental payload mismatch");

    // Restore ALL state
    hash_value = saved_hash;
    store = saved_store;
    std::memcpy(slot, saved_slot, 52);
    std::memcpy(canonical_pos, saved_canonical, 52);
    std::memcpy(children, saved_children, 52);
    std::memcpy(class_sum, saved_class_sum, sizeof(class_sum));
    std::memcpy(classes.class_members, saved_class_members, 52);
    std::memcpy(descriptors, saved_descriptors, sizeof(descriptors));
}
#endif
```

**Validation:**
1. `python3 scripts/run_tests.py` — all 3 gates must pass
2. **Critical:** Run debug build on Level 1 regression instances. The debug
   assert in `verify_against_scratch` fires on every `make_move`/`undo_move`:
   ```bash
   ./build.sh --debug --unit-tests
   cd cmake-build-debug && ctest -R regression_level1 --output-on-failure
   ```
   If any assertion fails, the incremental update has a bug.
3. Run solver manually on a few seeds to confirm no crashes:
   ```bash
   ./cmake-build-debug/bin/solvitaire --type klondike --random 1 \
       --cache-type multiplicity --timeout 30000
   ```

---

### 4.4 — Unit Tests for NONE Mode Incremental

**Goal:** Add targeted unit tests for the NONE mode incremental path.

**File:** New: `src/test/unit_tests/multiplicity_incremental_test.cpp`
**File:** Modified: `CMakeLists.txt` — add to `sources_test_unit`

**Tests to write:**

1. **`NoneModeSingleCardMove`** — Move one card in NONE mode. Assert
   incremental hash/payload matches from-scratch.

2. **`NoneModeWithReveal`** — Move with reveal. Two descriptor changes.
   Assert match.

3. **`NoneModeChildrenUpdate`** — After a move, verify `children[]` is
   correct by comparing to a fresh `rebuild_auxiliary()`.

4. **`NoneModeClassSumUpdate`** — After a move, verify `class_sum[]` entries
   match freshly computed values.

5. **`NoneModeLocativeChange`** — Card moves from cell to foundation (locative
   to locative). Assert correct update.

6. **`NoneModePredecessorChange`** — Card moves between tableau piles
   (predecessor to predecessor). Assert correct update.

7. **`NoneModeSequenceOfMoves`** — Apply 5-10 moves and undos. Assert
   incremental result matches from-scratch at every step.

**Test structure:** Each test constructs a `multiplicity_descriptor_engine`,
populates descriptors manually, calls `recompute_from_descriptors()` to get
the baseline, then calls `incremental_update_none()` with specific changes
and compares against a fresh `recompute_from_descriptors()` with the same
descriptor state.

**Validation:** `python3 scripts/run_tests.py --quick`

---

### 4.5 — Performance Baseline (NONE mode)

**Goal:** Benchmark incremental vs from-scratch on klondike seeds 1-50.

**Method:** Temporarily add a timing wrapper around the make_move/undo_move
multiplicity update. Compare wall-clock time and states/second for:
- `--cache-type multiplicity` with incremental (Stage 4)
- `--cache-type multiplicity` with from-scratch (revert to `recompute_all()`)
- `--cache-type auto` (flat cache baseline)

**This is a measurement task, not a code change.** Results go in
`docs/multiplicity-encoding/stage4-benchmark-results.md`.

---

## Stage 5: Incremental, With Symmetry (Cascade)

### 5.0 — Implement `incremental_update()` with BFS Cascade

**Goal:** Implement the full incremental update algorithm from the Stage 3
proposal, handling all three cascade triggers.

**File:** `src/main/game/multiplicity_descriptor_engine.h`

**Add a public method:**

```cpp
// Full incremental update with BFS cascade (COLOUR / SUIT_IRRELEVANT modes).
// Also works for NONE mode (cascade never fires, behaves like
// incremental_update_none but with slightly more overhead).
void incremental_update(
    const std::pair<uint8_t, multiplicity_descriptor>* changes,
    uint8_t n_changes);
```

**Implementation (following Stage 3 proposal exactly):**

```cpp
void incremental_update(
    const std::pair<uint8_t, multiplicity_descriptor>* changes,
    uint8_t n_changes)
{
    // ── Step 0: Descriptor update ────────────────────────────────────────

    // dirty_classes: bitmask of static class IDs that need re-sorting.
    // Max 26 classes for COLOUR, 13 for SI, so uint32_t suffices.
    uint32_t dirty_classes = 0;
    changed_mask = 0;

    for (uint8_t i = 0; i < n_changes; i++) {
        uint8_t c = changes[i].first;
        const auto& new_d = changes[i].second;
        const auto& old_d = descriptors[c];

        // Update children[]
        if (old_d.is_predecessor)
            children[old_d.predecessor_card_id] = -1;
        if (new_d.is_predecessor)
            children[new_d.predecessor_card_id] = static_cast<int8_t>(c);

        // Save old slot byte
        old_slot_save[c] = slot[c];

        // Update descriptor
        descriptors[c] = new_d;

        // Compute new slot byte using current canonical_pos
        uint8_t new_s = raw_slot(c);
        if (new_s != slot[c]) {
            slot[c] = new_s;
            dirty_classes |= (1u << classes.class_of[c]);
            changed_mask |= (1ULL << c);
        }
    }

    // ── Step 1: BFS cascade ─────────────────────────────────────────────

    int cascade_iter = 0;
    while (dirty_classes != 0) {
        assert(cascade_iter < 20 && "cascade did not converge");
        uint32_t next_dirty = 0;

        // Process each dirty class
        for (uint8_t cls = 0; cls < classes.n_classes; cls++) {
            if (!(dirty_classes & (1u << cls))) continue;

            const uint8_t base = classes.class_start[cls];

            // Re-sort class members by (slot[member], member)
            sort_class(classes.class_members + base, classes.class_size);

            // Reassign canonical_pos
            for (uint8_t j = 0; j < classes.class_size; j++) {
                canonical_pos[classes.class_members[base + j]] = base + j;
            }

            // Check children of ALL members
            for (uint8_t j = 0; j < classes.class_size; j++) {
                uint8_t m = classes.class_members[base + j];
                int8_t child = children[m];
                if (child < 0) continue;
                uint8_t uc = static_cast<uint8_t>(child);
                if (!descriptors[uc].is_predecessor) continue;

                uint8_t pos = collapsed_pos(m);
                uint8_t new_s = descriptors[uc].face_down
                    ? static_cast<uint8_t>(255 - pos) : pos;

                if (new_s != slot[uc]) {
                    if (!(changed_mask & (1ULL << uc))) {
                        old_slot_save[uc] = slot[uc];  // first change
                    }
                    slot[uc] = new_s;
                    changed_mask |= (1ULL << uc);
                    next_dirty |= (1u << classes.class_of[uc]);
                }
            }
        }

        dirty_classes = next_dirty;
        cascade_iter++;
    }

    // ── Step 2: Post-cascade update ─────────────────────────────────────

    // Collect affected classes from changed_mask
    uint32_t affected_classes = 0;
    uint64_t mask = changed_mask;
    while (mask) {
        uint8_t c = __builtin_ctzll(mask);  // find lowest set bit
        affected_classes |= (1u << classes.class_of[c]);
        mask &= mask - 1;  // clear lowest set bit
    }

    // Rebuild payload and hash for affected classes
    for (uint8_t cls = 0; cls < classes.n_classes; cls++) {
        if (!(affected_classes & (1u << cls))) continue;

        const uint8_t base = classes.class_start[cls];

        // XOR out old class sum
        hash_value ^= class_sum[cls];

        // Recompute class sum and payload entries
        uint64_t sum = 0;
        for (uint8_t j = 0; j < classes.class_size; j++) {
            uint8_t m = classes.class_members[base + j];
            store.set_slot(base + j, slot[m]);
            sum += zob_for_card(cls, m);
        }
        class_sum[cls] = sum;

        // XOR in new class sum
        hash_value ^= class_sum[cls];
    }
}
```

**Portability note:** `__builtin_ctzll` is GCC/Clang. For MSVC, use
`_BitScanForward64`. Or replace the bit-scan loop with a simple iteration
over all 52 cards checking `changed_mask & (1ULL << c)`.

**Bitmask sizing:** `dirty_classes` uses `uint32_t` (max 26 classes for
COLOUR). `changed_mask` uses `uint64_t` (52 cards fit in 52 bits).

**Validation:** Compile-only at this point. Wired in and tested in 5.1.

---

### 5.1 — Wire Full Incremental into `make_move` / `undo_move`

**Goal:** Replace the NONE-mode-only guard with the full incremental path.

**File:** `src/main/game/search-state/game_state.cpp`

**Replace the Stage 4 guard:**

```cpp
if constexpr (Policy::computes_multiplicity_descriptor) {
    if (mult_n_changes > 0) {
        desc_engine.incremental_update(mult_changes, mult_n_changes);
    } else {
        // Fallback for complex move types (stock_k_plus, stock_to_all_tableau)
        desc_engine.recompute_all(make_desc_ctx());
    }
#ifndef NDEBUG
    desc_engine.verify_against_scratch(make_desc_ctx());
#endif
}
```

The move-type-specific change identification from Stage 4.3 already covers
`regular`, `built_group`, and reveals. No additional work needed for COLOUR
/ SUIT_IRRELEVANT — the same descriptor changes feed into the cascade.

**Validation:**
1. `python3 scripts/run_tests.py` — all 3 gates
2. **Critical:** Debug build on Level 1 regression with symmetric games:
   ```bash
   ./cmake-build-debug/bin/solvitaire --type klondike --random 1 \
       --cache-type multiplicity --streamliners suit-symmetry --timeout 30000
   ```
   ```bash
   ./cmake-build-debug/bin/solvitaire --type free-cell --random 1 \
       --cache-type multiplicity --streamliners suit-symmetry --timeout 30000
   ```
   ```bash
   ./cmake-build-debug/bin/solvitaire --type black-hole --random 1 \
       --cache-type multiplicity --timeout 30000
   ```

---

### 5.2 — Unit Tests for Cascade

**Goal:** Add unit tests targeting each cascade trigger.

**File:** `src/test/unit_tests/multiplicity_incremental_test.cpp` (extend)

**Tests to add:**

1. **`CascadeSortReorder`** — COLOUR mode. Set up two class members with
   distinct slot bytes and children. Change one member's slot byte so the
   sort order flips. Assert children's slot bytes update correctly.

2. **`CascadeDynamicClassMerge`** — COLOUR mode. Two members with different
   slot bytes, each with a child. Change one member's descriptor to match
   the other (merge). Assert children see collapsed_pos.

3. **`CascadeDynamicClassSplit`** — COLOUR mode. Two members with same slot
   byte (same dynamic class), each with a child. Change one member's
   descriptor (split). Assert children see divergent collapsed_pos.

4. **`CascadeMultiLevel`** — COLOUR mode. Set up a predecessor chain through
   two classes (the ping-pong scenario from Stage 3 proposal §8.3). Assert
   cascade converges and result matches from-scratch.

5. **`CascadeRevealFaceDown`** — COLOUR mode. Reveal a face-down card.
   Assert the reflected-encoding slot byte change propagates correctly.

6. **`SuitIrrelevantFourWayCascade`** — SI mode (class_size=4). Set up a
   four-member class with predecessor chains. Assert cascade handles 4-way
   interactions correctly.

7. **`IncrementalMatchesFromScratch_Klondike`** — Klondike COLOUR mode.
   Apply 20 random moves/undos. Assert incremental matches from-scratch
   at every step (integration test).

8. **`IncrementalMatchesFromScratch_FreeCell`** — Free-cell SI mode.
   Same as above.

**Test methodology:** Each test constructs a `multiplicity_descriptor_engine`,
populates descriptors, calls `recompute_from_descriptors()`, then applies
one or more incremental changes and compares against a fresh from-scratch
computation with the same final descriptor state.

**Validation:** `python3 scripts/run_tests.py --quick`

---

### 5.3 — Solvability Cross-Check (Level 2)

**Goal:** Verify that the incremental multiplicity cache produces the same
solvability verdicts as the from-scratch multiplicity cache and the LRU
baseline.

**Method:** Same as Stage 2C Level 2 (see `stage2c-testing-plan.md`):

```bash
# Klondike COLOUR mode, seeds 1-20
for seed in $(seq 1 20); do
    ./cmake-build-release/bin/solvitaire --type klondike --random $seed \
        --cache-type multiplicity --streamliners suit-symmetry \
        --timeout 120000 --json
done

# Free-cell SI mode, seeds 1-20
for seed in $(seq 1 20); do
    ./cmake-build-release/bin/solvitaire --type free-cell --random $seed \
        --cache-type multiplicity --streamliners suit-symmetry \
        --timeout 120000 --json
done

# Black-hole SI mode, seeds 1-20
for seed in $(seq 1 20); do
    ./cmake-build-release/bin/solvitaire --type black-hole --random $seed \
        --cache-type multiplicity --timeout 120000 --json
done

# TABLEAU_PILES games, seeds 1-10
for game in east-haven spiderette will-o-the-wisp; do
    for seed in $(seq 1 10); do
        ./cmake-build-release/bin/solvitaire --type $game --random $seed \
            --cache-type multiplicity --timeout 120000 --json
    done
done
```

Compare verdicts against the Stage 2C Level 2 results. Zero mismatches
required.

Also run the no-symmetry regression (seeds 1-20 klondike, multiplicity vs
auto) to verify the NONE mode incremental path doesn't regress.

**Validation:** Zero solvability mismatches. Record results in
`docs/multiplicity-encoding/stage5-solvability-results.md`.

---

### 5.4 — Performance Benchmark

**Goal:** Measure the speedup from incremental updates on symmetric games.

**Method:** Run klondike seeds 1-150 with suit-symmetry, comparing:
- Incremental multiplicity (Stage 5)
- From-scratch multiplicity (revert to `recompute_all()`)
- LRU cache (baseline for symmetric games)

Record:
- Wall-clock time per seed
- States/second
- `states_searched` count (should be identical between incremental and
  from-scratch multiplicity)

**Validation:** Incremental should be significantly faster than from-scratch.
States_searched must match exactly (same cache decisions, same search tree).

Record results in `docs/multiplicity-encoding/stage5-benchmark-results.md`.

---

## Testing Summary

| Gate | When | What |
|---|---|---|
| 3-gate quick | After every sub-task | `python3 scripts/run_tests.py --quick` |
| 3-gate full | After 4.3, 5.1 | `python3 scripts/run_tests.py` |
| Debug regression | After 4.3, 5.1 | Debug build, Level 1 regression with `--cache-type multiplicity` |
| Debug assert | Every move | `verify_against_scratch()` in debug builds |
| Solvability cross-check | After 5.1 | Seeds x game types vs LRU baseline |
| Performance benchmark | After 4.5, 5.4 | Wall-clock comparison |

---

## Files Modified (Summary)

| File | Stage | Changes |
|---|---|---|
| `multiplicity_descriptor_engine.h` | 4.0-4.3, 5.0 | Add auxiliary data, `rebuild_auxiliary()`, `incremental_update_none()`, `incremental_update()`, `verify_against_scratch()`, descriptor helpers |
| `game_state.cpp` | 4.3, 5.1 | Wire incremental updates into make_move/undo_move per move type |
| `CMakeLists.txt` | 4.4 | Add `multiplicity_incremental_test.cpp` to `sources_test_unit` |
| New: `multiplicity_incremental_test.cpp` | 4.4, 5.2 | Unit tests for NONE mode and cascade |
| New: `stage4-benchmark-results.md` | 4.5 | NONE mode performance data |
| New: `stage5-solvability-results.md` | 5.3 | Solvability cross-check results |
| New: `stage5-benchmark-results.md` | 5.4 | Symmetry mode performance data |

---

## Risk Mitigation

| Risk | Mitigation |
|---|---|
| Incremental/from-scratch mismatch | `verify_against_scratch()` on every move in debug; Level 1 regression in debug build catches all |
| Cascade non-convergence | `assert(cascade_iter < 20)` — same guard as from-scratch fixpoint |
| Performance regression | Benchmark at 4.5 and 5.4; if incremental is slower, investigate before proceeding |
| Complex move types (stock_k_plus) | Fall back to `recompute_all()` — no correctness risk, only performance |
| Bitmask overflow | `dirty_classes` in `uint32_t` supports up to 32 classes (max is 26 for COLOUR); `changed_mask` in `uint64_t` supports 52 cards |

---

## Ordering and Dependencies

```
Stage 4.0 (auxiliary data)
    |
Stage 4.1 (descriptor helper)
    |
Stage 4.2 (incremental_update_none)
    |
Stage 4.3 (wire into make_move/undo_move)
    |
Stage 4.4 (unit tests)
    |
Stage 4.5 (benchmark)
    |
Stage 5.0 (incremental_update with cascade)
    |
Stage 5.1 (wire full incremental)
    |
Stage 5.2 (cascade unit tests)
    |
Stage 5.3 (solvability cross-check)
    |
Stage 5.4 (benchmark)
```

All stages are sequential. Each depends on the previous.

---

## Important Notes for Implementer

1. **Do not delete `recompute_all()`**. It is the debug-mode oracle and must
   remain permanently.

2. **All three test gates must pass after every sub-task.** If a gate fails,
   fix it before proceeding.

3. **The debug-mode `verify_against_scratch()` assertion is the primary
   correctness check.** If it fires, the incremental update has a bug. Do not
   disable it.

4. **`stock_k_plus` and `stock_to_all_tableau` use `recompute_all()` as
   fallback.** This is intentional — those moves are complex and the
   incremental benefit is small.

5. **Accordion and sequence games do not use MultiplicityPolicy.** No changes
   needed for those move types.

6. **Read the Stage 3 proposal** (`stage3-incremental-update-proposal.md`)
   for the full algorithm rationale, worked examples, and termination argument.

7. **If you get stuck or the approach seems wrong, ask Ian.** Do not guess
   at game semantics or descriptor meanings.
