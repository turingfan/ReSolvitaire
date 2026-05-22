# Stage 3 — Incremental Update Algorithm Proposal

**Date:** 2026-05-20
**Status:** Draft for review
**Authors:** Ian Gent & AI Assistant (Claude Opus 4.6)
**Prerequisite:** Stages 0-2 complete and validated (from-scratch canonicalisation)

## 1. Overview

This document specifies the incremental update algorithm for the multiplicity
encoding cache. Currently (Stages 1-2), `recompute_all()` rebuilds all 52
descriptors, runs the full fixpoint canonicalisation, and rewrites the entire
payload and hash after every `make_move` / `undo_move`. The incremental
algorithm replaces this with targeted updates proportional to the number of
cards affected by the move.

The algorithm has three steps:

1. **Descriptor update** — identify cards whose descriptors changed, update
   descriptors and the `children[]` auxiliary structure, compute new slot bytes
2. **BFS cascade** — re-sort dirty static classes, propagate canonical position
   changes to children via `collapsed_pos`, collect newly-dirty classes, repeat
   until stable
3. **Post-cascade update** — rebuild payload entries and Zobrist class sums for
   all affected classes

In NONE mode (no symmetry), Step 2 is skipped entirely: canonical positions
are identity-mapped and never change, so the cascade cannot fire. The update
is O(changed cards).

**Relationship to v5.1 spec.** Section "Cascading Updates (overview)" in
`multiplicity_encoding_v5.tex` gives the high-level picture and explicitly
defers the detailed algorithm to this document. The three cascade triggers
(sort reordering, dynamic class merge, dynamic class split) are defined there;
this document specifies how to detect and handle each one.

---

## 2. Auxiliary Data Structures

Three new persistent structures are maintained across the DFS, in addition to
the existing `slot[52]`, `canonical_pos[52]`, and `class_members[52]`:

### 2.1 `children[52]`

For each card `q`, the card whose descriptor is `predecessor(q, _)`, or -1 if
no such card exists.

**Invariant:** `children[q] == c` iff `descriptors[c].is_predecessor &&
descriptors[c].predecessor_card_id == q`.

**Capacity:** At most 1 child per card in all supported games (each card
occupies exactly one pile position; at most one card sits on it).

**Initialisation:** Walk all piles once during `init()`. For each card `c`
with a predecessor descriptor pointing to `q`, set `children[q] = c`.

**Maintenance:** Updated in Step 0 of each incremental update (see below).

### 2.2 `class_sum[n_classes]`

Per-static-class Zobrist sum: `class_sum[cls] = sum of zob_for_card(cls, m)`
for all members `m` of class `cls`. The full hash is the XOR of all class sums:

```
hash_value = class_sum[0] ^ class_sum[1] ^ ... ^ class_sum[n_classes - 1]
```

**Initialisation:** Computed during the first from-scratch `recompute_all()`,
alongside `hash_value`.

**Maintenance:** Updated in Step 2 of each incremental update.

### 2.3 `old_slot[52]` (scratch)

Temporary array used within a single incremental update to save the pre-update
slot byte of each changed card. Needed for Zobrist delta computation. Not
persistent across moves — only meaningful during the update.

---

## 3. Step 0: Descriptor Update

### 3.1 Identifying changed cards

After the physical move is applied (pile operations complete), determine which
cards had their descriptors change. This depends on the move type:

| Move type | Cards with changed descriptors |
|---|---|
| `regular` | 1 (moved card) + optionally 1 (revealed card) |
| `built_group` | 1 (bottom of group) + optionally 1 (revealed card) |
| `stock_k_plus` | k (stock-to-waste) + 1 (waste-to-foundation) |
| `stock_to_all_tableau` | p (one per tableau pile receiving a card) |
| `sequence` | 1 (moved card) + optionally 1 (revealed card) |
| `accordion` | 1 (moved card) + 1 (old successor, if any) |

For **regular** and **built_group** moves, a **reveal** adds one more changed
card: the card that was face-down and is now turned face-up. Its descriptor
changes only in the `face_down` flag (same predecessor or locative kind).

For **accordion** moves, the moved card's old successor (the card that was
sitting on the moved card's old position) now sits on a different card or is
at a pile bottom. This changes the successor's descriptor.

### 3.2 Algorithm

```
function step0_descriptor_update(changed_cards):
    dirty_classes = {}
    changed_set = {}

    for each (c, new_descriptor) in changed_cards:
        old_d = descriptors[c]

        # Update children[]
        if old_d.is_predecessor:
            children[old_d.predecessor_card_id] = -1
        if new_descriptor.is_predecessor:
            children[new_descriptor.predecessor_card_id] = c

        # Save old slot byte, update descriptor
        old_slot[c] = slot[c]
        descriptors[c] = new_descriptor

        # Compute new slot byte using current canonical_pos
        new_s = raw_slot(c)
        if new_s != slot[c]:
            slot[c] = new_s
            dirty_classes.add(class_of[c])
            changed_set.add(c)

    return (dirty_classes, changed_set)
```

**Note on `raw_slot`:** For predecessor descriptors, `raw_slot(c)` uses the
current `canonical_pos` of the predecessor card (carried forward from the
previous move's fixpoint). This is a valid starting point for the cascade; the
cascade will correct it if the predecessor's class needs re-sorting.

For locative descriptors, `raw_slot` returns `52 + kind` (or reflected for
face-down), independent of any canonical position.

---

## 4. Step 1: BFS Cascade

### 4.1 Why cascades happen

A slot byte change in class X may change the sort order of X, which changes
`canonical_pos` for X's members, which changes `collapsed_pos` for X's members,
which changes the slot bytes of children whose predecessor points into X. Those
children are in some class Y, which may itself need re-sorting. This is the
cascade.

The three triggers from the v5.1 spec:

1. **Sort reordering.** A member's slot byte changes, causing it to swap
   position with another member. `canonical_pos` values change. Children
   referencing the affected members see different `collapsed_pos` values.

2. **Dynamic class merge.** Two members that had distinct slot bytes now have
   equal slot bytes. They form a larger dynamic class. `collapsed_pos` for
   both becomes the lowest `canonical_pos` in the merged group. Children of
   either member now see the same (lower) `collapsed_pos`.

3. **Dynamic class split.** Two members that had equal slot bytes now have
   different slot bytes. The dynamic class splits. `collapsed_pos` for each
   becomes its own `canonical_pos` (or the lowest in its new subgroup).
   Children that previously saw the collapsed value now see a different value.

All three triggers are handled uniformly by the BFS cascade: re-sort the
dirty class, then check all children of all members.

### 4.2 Algorithm

```
function step1_bfs_cascade(dirty_classes, changed_set):
    while dirty_classes is not empty:
        next_dirty = {}

        for each cls in dirty_classes:
            base = class_start[cls]

            # Re-sort class members by (slot[member], member)
            sort_class(class_members + base, class_size)

            # Reassign canonical_pos
            for i in 0 .. class_size-1:
                canonical_pos[class_members[base + i]] = base + i

            # Check children of ALL members of this class
            for i in 0 .. class_size-1:
                m = class_members[base + i]
                c = children[m]
                if c != -1 and descriptors[c].is_predecessor:
                    pos = collapsed_pos(m)
                    new_s = face_down_reflect(pos, descriptors[c].face_down)
                    if new_s != slot[c]:
                        if c not in changed_set:
                            old_slot[c] = slot[c]   # first change for c
                        slot[c] = new_s
                        changed_set.add(c)
                        next_dirty.add(class_of[c])

        dirty_classes = next_dirty

    return changed_set
```

Where:

```
face_down_reflect(pos, fd) = fd ? (255 - pos) : pos
```

### 4.3 Why we check ALL members, not just the changed one

When a class is re-sorted, `canonical_pos` may change for ALL members, even
those whose slot bytes didn't change. A member whose slot byte is unchanged
may still have its `collapsed_pos` change due to:

- A neighbouring member joining its dynamic class (merge)
- A neighbouring member leaving its dynamic class (split)
- A swap in sort position with another member

Since `collapsed_pos` depends on the full sorted structure of the class, we
must check children of every member, not just the one that triggered the
re-sort.

### 4.4 NONE mode: no cascade

In NONE mode, every static class has exactly 1 member. Re-sorting a singleton
is a no-op. `canonical_pos[c] = c` always. `collapsed_pos(c) = c` always.
Children's slot bytes depend on `canonical_pos` of their predecessor, which is
identity-mapped and never changes.

Therefore: Step 1 can be skipped entirely in NONE mode. The incremental update
is just Step 0 (update descriptors, compute new slot bytes) + Step 2 (update
payload and hash). Cost: O(changed cards).

---

## 5. Step 2: Post-Cascade Update

After the cascade stabilises, update the payload and Zobrist hash for all
affected classes.

```
function step2_post_cascade(changed_set):
    # Collect affected classes
    affected_classes = { class_of[c] : c in changed_set }

    for each cls in affected_classes:
        base = class_start[cls]

        # XOR out old class sum
        hash_value ^= class_sum[cls]

        # Recompute class sum and payload entries
        class_sum[cls] = 0
        for i in 0 .. class_size-1:
            m = class_members[base + i]
            store.set_slot(base + i, slot[m])
            class_sum[cls] += zob_for_card(cls, m)

        # XOR in new class sum
        hash_value ^= class_sum[cls]
```

**Cost:** O(`class_size` x number of affected classes). For COLOUR mode
(`class_size = 2`), this is O(2 x |affected|). For SUIT_IRRELEVANT
(`class_size = 4`), O(4 x |affected|).

**Why full class recomputation, not per-card deltas.** With `class_size <= 4`,
iterating over all members is no more expensive than maintaining per-card
deltas and avoids bookkeeping complexity. The sort in Step 1 may have changed
the mapping from members to payload positions, so we must rewrite all entries
in the class anyway.

---

## 6. Undo

Undo uses the same three-step algorithm, applied to the reverse descriptor
change. No snapshots or saved state are needed beyond the move record.

When `undo_move(m)` restores the physical pile state:
1. Compute the reverse descriptor changes (each card returns to its pre-move
   descriptor, derivable from the restored pile state)
2. Run Step 0 with these reverse changes
3. Run Step 1 (BFS cascade)
4. Run Step 2 (post-cascade update)

**Correctness argument.** The canonical sort order, slot bytes, payload, and
hash are deterministic functions of the descriptors and the static class
structure. Since undo restores the descriptors to their pre-move values, the
algorithm produces the same result as the pre-move state — regardless of
the path taken to reach it.

**`children[]` is reverted automatically:** Step 0 undoes the children
updates from the forward move (removing the child from the new predecessor,
adding it back to the old predecessor).

**`class_sum[]` is recomputed in Step 2:** No saved values needed.

---

## 7. Worked Examples

All examples use COLOUR mode: 26 classes of 2 (black = {Clubs, Spades},
red = {Hearts, Diamonds}). Class "black aces" = {AC (cid 0), AS (cid 26)}.

### 7.1 Simple update, no cascade

**State:**
- AC in a cell: descriptor = `locative(IN_CELL, false)`, slot = 52
- AS on 2H (cid 14): descriptor = `predecessor(14, false)`, slot = 7
- Sorted: AS (7) before AC (52). `canonical_pos`: AS=0, AC=1
- No card sits on AS or AC: `children[0] = children[26] = -1`

**Move:** AC moves from cell to foundation.

**Step 0:**
- AC's descriptor changes from `locative(IN_CELL)` to `locative(PERMANENT)`
- `children[]` unchanged (no predecessor descriptors involved)
- AC's new slot = 52 + 1 = 53 (PERMANENT). Was 52. Changed.
- dirty = {class "black aces"}, changed = {AC}

**Step 1:**
- Re-sort: AS (7) before AC (53). Same order as before.
- `canonical_pos` unchanged.
- Check children of AS: -1. Check children of AC: -1.
- next_dirty = {}. Cascade ends.

**Step 2:**
- Affected classes = {"black aces"}
- XOR out old class_sum, recompute, XOR in new class_sum
- Rewrite payload positions 0 and 1

**Result:** O(1) total work. No cascade.

### 7.2 Cascade trigger: dynamic class merge

**State:**
- AC in a cell: descriptor = `locative(IN_CELL, false)`, slot = 52
- AS in a cell: descriptor = `locative(IN_CELL, false)`, slot = 52
- Both have slot 52 → same dynamic class. `collapsed_pos(AC) = 0`,
  `collapsed_pos(AS) = 0` (lowest canonical_pos in group).
- 3C (cid 2) sits on AC: descriptor = `predecessor(0, false)`,
  slot = `collapsed_pos(0)` = 0
- 3S (cid 28) sits on AS: descriptor = `predecessor(26, false)`,
  slot = `collapsed_pos(26)` = 0

**Move:** AS moves from cell to tableau, now sits on 2H (cid 14).

**Step 0:**
- AS's descriptor changes from `locative(IN_CELL)` to `predecessor(14, false)`
- children[14] = AS (26)
- AS's new slot = `canonical_pos[14]` = (whatever 2H's position is, say 7).
  Was 52. Changed.
- dirty = {"black aces"}, changed = {AS}

**Step 1, iteration 1:**
- Re-sort "black aces": AS (7) before AC (52). Swap! canonical_pos: AS=0, AC=1.
- AC and AS now have different slot bytes → **split**. Previous dynamic class
  {AC, AS} at slot 52 splits into {AS} at slot 7 and {AC} at slot 52.
- `collapsed_pos(AC)` = 1 (was 0). `collapsed_pos(AS)` = 0 (unchanged).
- Check children:
  - `children[AS=26]` = 3S (28). 3S's slot = `collapsed_pos(26)` = 0.
    Was 0. No change.
  - `children[AC=0]` = 3C (2). 3C's slot = `collapsed_pos(0)` = 1.
    Was 0. **Changed!** → dirty "black threes", changed += {3C}
- next_dirty = {"black threes"}

**Step 1, iteration 2:**
- Re-sort "black threes": 3C (1) before 3S (0)? No: 3S (0) before 3C (1).
  Check if this is different from before: before, both had slot 0 and were
  sorted by card_id (3C=2 before 3S=28). Now 3S (0) before 3C (1). Same
  order? 3C was at canonical_pos 0 before; now 3S is at canonical_pos 0.
  Swapped. canonical_pos: 3S=base, 3C=base+1.
- `collapsed_pos(3S)` = base (was base, when both were in same dynamic
  class). `collapsed_pos(3C)` = base+1 (was base).
- Check children of 3S and 3C: suppose neither has children. next_dirty = {}.
  Cascade ends.

**Step 2:** Rebuild "black aces" and "black threes" payload entries and sums.

### 7.3 Cascade trigger: sort reordering without merge/split

**State:**
- AC sits on 5H (cid 17): slot = `canonical_pos[17]` = 9
- AS sits on 3D (cid 41): slot = `canonical_pos[41]` = 23
- Sorted: AC (9) before AS (23). canonical_pos: AC=0, AS=1.
- 2S (cid 27) sits on AC: slot = `collapsed_pos(0)` = 0
  (AC is in its own dynamic class, collapsed_pos = canonical_pos = 0)

**Move:** AC moves to sit on KH (cid 24) instead.

**Step 0:**
- AC's descriptor: `predecessor(17, false)` → `predecessor(24, false)`
- children[17] = -1, children[24] = AC
- AC's new slot = `canonical_pos[24]` = say 40. Was 9. Changed.
- dirty = {"black aces"}, changed = {AC}

**Step 1, iteration 1:**
- Re-sort: AS (23) before AC (40). **Swapped!** canonical_pos: AS=0, AC=1.
- Neither merged nor split — different slot bytes before and after. But
  canonical positions swapped.
- `collapsed_pos(AC)` = 1 (was 0). `collapsed_pos(AS)` = 0 (was 1).
- Check children:
  - `children[AC=0]` = 2S (27). 2S's slot = `collapsed_pos(0)` = 1.
    Was 0. **Changed!** → dirty class of 2S.
  - `children[AS=26]`: suppose some card c sits on AS. c's slot =
    `collapsed_pos(26)` = 0. If c's old slot was 1 (from the previous
    `collapsed_pos(AS)` = 1): **Changed!** → dirty class of c.

This demonstrates pure sort reordering propagating to children.

### 7.4 Reveal (face-down flag change)

**State:**
- Card R (cid 15) is face-down, sits on card Q (cid 30):
  descriptor = `predecessor(30, true)`, slot = `255 - canonical_pos[30]`

**Move:** Card above R is moved away, R is revealed.

**Step 0:**
- R's descriptor: `predecessor(30, true)` → `predecessor(30, false)`
- `children[]` unchanged (same predecessor, only face_down changed)
- R's new slot = `canonical_pos[30]`. Was `255 - canonical_pos[30]`.
  Large change in slot byte. Changed.
- dirty = {class_of[R]}, changed = {R}

**Step 1:** Re-sort R's class (R moved from near 255 to near 0 in slot
order). canonical_pos changes. Children of R's class members are checked.
Cascade may propagate.

---

## 8. Termination

### 8.1 Guaranteed convergence

The BFS cascade converges because:

1. The state space (the vector of all 52 slot bytes) is finite.
2. The update operator (sort + collapsed_pos + propagate) is deterministic.
3. Therefore the cascade either converges to a fixed point or enters a cycle.
4. The fixpoint operator is the same one used by the from-scratch algorithm
   (`recompute_from_descriptors`), which is known to converge — it was
   validated in Stage 2C with the `assert(iter < 20)` bound.

The from-scratch fixpoint converges because each iteration can only merge
dynamic classes (via Scheme A collapsing), never split them, when starting
from raw slot bytes. For the incremental cascade, we start from a perturbed
fixpoint; the perturbation can cause splits, but the subsequent iterations
re-converge to the unique fixpoint for the current descriptor set.

### 8.2 Depth bound

Each BFS level follows predecessor edges downward (from parent toward child,
i.e., toward pile tops). The predecessor graph is a forest — each card has
at most one predecessor, and cycles are impossible (the "sits on" relation
is acyclic within any pile).

**Simple bound:** maximum predecessor chain depth in single-deck is 12
(a 13-card pile has 12 predecessor edges). So at most 12 BFS levels.

**Cross-class interaction.** When members of a class appear in different
predecessor chains, the cascade can alternate between classes at successive
BFS levels (see the ping-pong scenario in section 8.3). However, each BFS
level still advances by one step along a predecessor chain. The total number
of BFS levels is bounded by the maximum depth across all chains involved.

### 8.3 Ping-pong scenario

Consider two classes X and Y whose members appear in interleaved positions
across different piles:

```
Pile 1: [3C, AC, 5H]   — 3C sits on AC, AC sits on 5H
Pile 2: [AS, 3S, 2C]   — AS sits on 3S, 3S sits on 2C
```

Class "black aces" = {AC, AS}. Class "black threes" = {3C, 3S}.
`children[AC] = 3C`, `children[3S] = AS`.

If "black aces" is dirty:
- Level 0: process aces → 3C's slot changes → "black threes" dirty
- Level 1: process threes → AS's slot changes → "black aces" dirty
- Level 2: process aces → 3C's slot may change → ...

This is not a violation of the depth bound: level 0 follows the edge
AC → 3C (depth 1→2 in pile 1), level 1 follows 3S → AS (depth 1→2 in
pile 2), level 2 follows AC → 3C again. The cascade traces two separate
predecessor chains in parallel.

**Convergence:** Each re-processing either produces no change (stable) or
moves the slot bytes closer to the fixpoint. Since the state space is finite
and the operator deterministic, cycles cannot occur. In practice, ping-pong
stabilises within 1-2 additional iterations after the initial perturbation.

**Implementation safeguard:** Retain the `assert(iter < MAX_CASCADE)` guard
(e.g., MAX_CASCADE = 20) to catch any unexpected non-convergence during
testing. This mirrors the existing `assert(iter < 20)` in the from-scratch
fixpoint.

---

## 9. Cost Analysis

### 9.1 Per-mode summary

| Mode | Step 0 | Step 1 (cascade) | Step 2 | Total (common case) |
|---|---|---|---|---|
| NONE (52x1) | O(k) | skipped | O(k) | **O(k)** |
| COLOUR (26x2) | O(k) | O(1) per dirty class | O(2 x d) | **O(k + d)** |
| SUIT_IRRELEVANT (13x4) | O(k) | O(1) per dirty class | O(4 x d) | **O(k + d)** |

Where k = number of cards with changed descriptors (typically 1-2),
d = number of dirty classes encountered across all cascade levels (typically 1).

### 9.2 Common case: regular move, no reveal

- k = 1 (one card moved)
- Step 0: 1 descriptor update, 1 children update, 1 slot byte computation
- Step 1: 1 class re-sorted (2 or 4 elements), 2 or 4 children checked,
  typically 0 propagations
- Step 2: 1 class rebuilt (2 or 4 payload writes, 1 sum recomputation)
- **Total: O(1)**

### 9.3 Worst case

Maximum cascade: all 12 levels fire, each dirtying one class. Total dirty
classes = 12. Per class: O(class_size) sort + O(class_size) children checks +
O(class_size) payload writes. With class_size = 4: O(4 x 12) = O(48).

The v5.1 spec's bound of O(chain_depth x class_multiplicity) <= 48 for
single-deck Scheme A is consistent with this analysis.

### 9.4 Comparison with from-scratch

From-scratch `recompute_all()` costs:
- Descriptor walk: O(52) to iterate all piles
- Fixpoint: O(52 x iterations) where iterations <= 20
- Payload write: O(52)
- Hash: O(52)
- **Total: O(52 x iterations)**, typically O(100-200)

The incremental algorithm is O(1) in the common case vs O(100+) for
from-scratch — roughly two orders of magnitude improvement on the DFS hot path.

---

## 10. Move-Type Details

### 10.1 Regular move

```
make_regular_move(m):
    moved = top card of pile m.from
    old_parent = pile below moved (if exists)
    new_parent = top of pile m.to (if non-empty, else IN_SPACE)

    changed = [(moved, new_descriptor_for(moved, m.to))]

    if m.reveal_move:
        revealed = new top of pile m.from (now face-up)
        changed.append((revealed, same_descriptor_but_face_up))

    incremental_update(changed)
```

### 10.2 Built group move

```
make_built_group_move(m):
    # Only the bottom card of the group changes descriptor
    bottom = pile[m.from][m.count - 1]  (0-indexed from top)
    new_parent = top of pile m.to (if non-empty, else IN_SPACE)

    changed = [(bottom, predecessor(new_parent) or locative(IN_SPACE))]

    if m.reveal_move:
        revealed = new top of pile m.from (now face-up)
        changed.append((revealed, same_descriptor_but_face_up))

    incremental_update(changed)
```

### 10.3 Stock/waste moves

All stock and waste cards have locative descriptors (IN_STOCK, IN_WASTE,
PERMANENT). No predecessor descriptors are involved. Therefore:
- No cascade can fire (locative slot bytes are independent of canonical_pos)
- Cost: O(k) direct slot byte + hash updates, where k = number of cards moved

### 10.4 Accordion move

The moved card changes descriptor. Its old successor (the card that was
sitting on the moved card) also changes descriptor if the moved card leaves
a pile. Both are included in `changed_cards`.

---

## 11. Data Structure Sizing

| Structure | Type | Size | Notes |
|---|---|---|---|
| `children[52]` | `int8_t[52]` | 52 B | -1 = no child; at most 1 child per card |
| `class_sum[52]` | `uint64_t[52]` | 416 B max | Indexed by class_id; 52 for NONE, 26 for COLOUR, 13 for SI |
| `old_slot[52]` | `uint8_t[52]` | 52 B | Scratch; only meaningful during update |
| `slot[52]` | `uint8_t[52]` | 52 B | Already exists |
| `canonical_pos[52]` | `uint8_t[52]` | 52 B | Already exists |
| `class_members[52]` | `uint8_t[52]` | 52 B | Already exists |

Total new memory: 520 bytes. All cache-friendly (fits in L1).

For the BFS cascade, `dirty_classes` and `next_dirty` can be implemented as
small fixed-size arrays with a count (max 26 classes for COLOUR, 13 for SI)
or as a 52-bit bitmask. No heap allocation needed.

`changed_set` can be a 64-bit bitmask (bits 0-51 for each card). O(1) insert
and membership test.

---

## 12. Testing Strategy

### 12.1 Debug-mode from-scratch comparison

The primary correctness check: in debug builds, after every incremental update,
run the from-scratch `recompute_from_descriptors()` and assert that:

1. `hash_value` matches
2. `store` matches (byte-for-byte comparison of slot data, bytes 3-54)
3. `canonical_pos[c]` matches for all c
4. `slot[c]` matches for all c
5. `class_members` ordering matches for all classes
6. `children[q]` matches the expected value derived from descriptors

This mirrors the existing `assert(iter < 20)` philosophy: the assertion runs
only in debug builds and has zero cost in release.

### 12.2 Exhaustive small-instance testing

Run the solver in debug mode on all Level 1 regression instances (150 seeds
across multiple game types). Every `make_move` and `undo_move` triggers the
from-scratch comparison. This exercises:

- All move types (regular, built_group, stock_k_plus, etc.)
- All symmetry modes (NONE, COLOUR, SUIT_IRRELEVANT)
- All cascade triggers (sort reorder, merge, split)
- Undo correctness (every state visited on the way down is revisited on undo)

### 12.3 Solvability cross-check

Same methodology as Stage 2C Level 2: run multiplicity cache with incremental
updates on the same seed set and compare solvability verdicts against the
from-scratch multiplicity cache and the flat/LRU baselines.

### 12.4 Performance validation

Benchmark incremental vs from-scratch on klondike seeds 1-150 with 60s
timeout. Measure:
- Wall-clock time
- States/second
- Percentage of moves with 0 cascade levels (expected: >95%)
- Maximum cascade depth observed

---

## 13. Implementation Stages

This proposal covers the algorithm for both Stage 4 (incremental, no symmetry)
and Stage 5 (incremental, with symmetry). The recommended implementation order:

**Stage 4** — NONE mode only:
- Implement `children[]` and `class_sum[]`
- Implement Step 0 and Step 2 (no Step 1 needed for NONE)
- Debug-mode from-scratch comparison on every move
- Performance benchmark

**Stage 5** — COLOUR and SUIT_IRRELEVANT modes:
- Add Step 1 (BFS cascade)
- All three cascade triggers exercised
- Debug-mode from-scratch comparison on symmetric games
- Full test suite

This split isolates the complexity: Stage 4 validates the descriptor-update
and Zobrist-maintenance machinery without cascade logic. Stage 5 adds only
the cascade, building on a tested foundation.

---

## 14. Open Questions for Review

1. **Accordion predecessor chains.** Accordion games can produce longer piles
   (via absorption). Does the cascade depth bound of 12 still hold? If not,
   what is the correct bound for accordion?

2. **Two-deck extension.** This proposal assumes single-deck (52 cards).
   Two-deck doubles the card count and may have larger static classes (up to
   8 members for two-deck suit-irrelevant). The algorithm generalises
   directly, but the cost bounds scale accordingly.

3. **Scheme B interaction.** If Scheme B is implemented later (distinct
   labelling within dynamic classes), the cascade triggers change. Sort
   reordering under Scheme B causes different label assignments but may reduce
   cascade propagation. The BFS structure remains the same.
