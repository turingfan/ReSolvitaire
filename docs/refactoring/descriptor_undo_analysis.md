# Descriptor Undo Reconstruction: Semantic Analysis

**Date:** 2026-04-10
**Status:** REVISED — STARTING_FACE_UP is now proven recoverable (see Section "Resolution")

---

## The Question

When undoing a move using the "pile-first" approach (reverse pile operations, then recompute descriptor values from restored pile state), can we always **uniquely determine the correct pre-move descriptor** for each card affected?

This document analyses every case systematically to identify where reconstruction works, where it fails, and what (if anything) needs to be stored to make it work.

---

## Descriptor Vocabulary

From `compact_state.h`:

| Value | Name | Meaning |
|---|---|---|
| 0 | STARTING | Face-down card, or card in stock/waste/reserve/foundation |
| 1 | STARTING_FACE_UP | Face-down card that was revealed (turned face-up) but not moved |
| 2 | ROOT | Card on a non-legal-build parent in the tableau |
| 3 | IN_CELL | Card in a cell pile |
| 4–7 | PARENT_0–3 | Card on a legal-build parent (index identifies which parent) |
| 8 | IN_HOLE | Card in the hole pile |
| 9 | IN_SPACE | Card at the bottom of a tableau pile (empty space below it) |

Key semantic distinctions:
- **STARTING vs STARTING_FACE_UP**: STARTING is face-down or in stock/waste/reserve/foundation. STARTING_FACE_UP is a card that was revealed in-place.
- **ROOT vs IN_SPACE**: Both are at/near the bottom of a tableau pile, but IN_SPACE means nothing is below, while ROOT means a non-legal-build card is below.
- The ROOT/IN_SPACE distinction was the subject of a critical bug fix (see `bug_report_root_descriptor_false_positives.md`).

---

## Component-by-Component Analysis

### Component 1: Moved Card Descriptor

**Question:** After undoing pile ops (card is back at `m.from`), can we call `determine_destination_descriptor(m.from, card)` to recover the pre-move descriptor?

**Analysis:**

`determine_destination_descriptor()` (lines 1180–1224 of `game_state.cpp`) computes a descriptor based on:
1. The pile type of the destination (foundation, hole, cell, tableau, or other)
2. The pile contents at the destination (empty → IN_SPACE; non-empty → lookup parent)

After pile undo, the card is back at `m.from` as the top card. The pile below it is exactly what it was before the original `make_move` call (because DFS undo is LIFO: all sub-moves have already been undone). Therefore `determine_destination_descriptor(m.from, card)` will examine the same pile context and return the same descriptor.

**Verdict: WORKS** — `determine_destination_descriptor(m.from, card)` recovers the pre-move descriptor after pile undo.

**One subtlety:** In `make_regular_move`, the function is called as `determine_destination_descriptor(m.to, moved)` *after* `place_card` (so `piles[m.to]` already contains the card). In the undo path (pile-first), the card would be at `m.from` after `place_card(m.from, take_card(m.to))`. The function checks `piles[dest].size() == 1` to detect empty-pile placement. After undo, `m.from` contains the card, so `piles[m.from].size() >= 1`. If the original pre-move m.from had only this card on top of face-down cards (size > 1), or had nothing below (size == 1), the function correctly reads it.

**Edge case — STARTING for foundation/stock/waste/reserve:** When `m.from` is a foundation, hole, cell, stock, waste, or reserve pile, the function returns the correct descriptor (STARTING, IN_HOLE, IN_CELL) based on pile type alone. No dependency on pile contents.

### Component 2: Revealed Card Descriptor

**Question:** When `m.reveal_move` is set, can we reconstruct the revealed card's pre-reveal descriptor?

**Analysis:**

During `make_regular_move`, a reveal happens after pile ops:
```cpp
if (m.reveal_move) {
    piles[m.from][0].turn_face_up();
    card rev = piles[m.from][0];
    rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());
    uint8_t rev_desc = (piles[m.from].size() == 1)
        ? compact_state::IN_SPACE
        : compact_state::STARTING_FACE_UP;
    update_card_descriptor(rev_cid, rev_desc);
}
```

The revealed card always had descriptor **STARTING** before the reveal (face-down cards always have STARTING — enforced by `check_face_down_consistent()` in debug builds).

When undoing, we need to set the descriptor back to **STARTING**. This is always correct regardless of pile state.

**But can we identify WHICH card was revealed?**

After pile undo (card placed back at `m.from`):
- The moved card is at `piles[m.from][0]` (top)
- The revealed card is at `piles[m.from][1]` (one below the returned card)

We can read `piles[m.from][1]` to get the card identity. We know it's the revealed card because `m.reveal_move` is set.

**Verdict: WORKS** — revealed card is always at `piles[m.from][1]` after pile undo; its pre-reveal descriptor is always STARTING.

**Subtlety for built-group moves:** In `make_built_group_move`, the reveal also happens after pile ops. After undoing the built group (all `m.count` cards returned to `m.from`), the revealed card is at `piles[m.from][m.count]` — one below the bottom of the returned group. But wait: the current `undo_built_group_move` does the reveal undo BEFORE the pile undo (the card is at `piles[m.from][0]`). In the pile-first approach, the cards are returned first, so the revealed card is at `piles[m.from][m.count]`. This needs to be indexed correctly but is deterministic.

### Component 3: Foundation Top Rank

**Question:** Can we recover the pre-move foundation top rank from restored pile state?

**Analysis:**

Two sub-cases:
1. **Source was foundation** (`m.from` is foundation): After pile undo, the card is placed back on the foundation. The foundation top is now the moved card again. The pre-move top rank IS the moved card's rank. We can read `piles[m.from].top_card().get_rank()`.

2. **Destination was foundation** (`m.to` is foundation): After pile undo, the card is removed from the foundation. The pre-move top was whatever was below the moved card. We read `piles[m.to].empty() ? 0 : piles[m.to].top_card().get_rank()`.

**Verdict: WORKS** — foundation ranks are directly readable from restored pile state.

### Component 4: Hole Top Card

**Question:** Can we recover the pre-move hole top card ID?

**Analysis:**

Only relevant when `m.to == hole` (card was placed in hole). After pile undo, the card is removed from the hole. The pile at `hole` now shows whatever was there before the move:
- If hole had cards before: `piles[hole].top_card()` gives the old hole top card.
- If hole was empty before: `piles[hole].empty()` is true.

We need to know the old `hole_top` value stored in the payload. Looking at `init_payload_and_hash()` (line 1068):
```cpp
if (rules.hole && !piles[hole].empty()) {
    card top = piles[hole].top_card();
    uint8_t cid = zobrist_hash::card_id(top.get_suit(), top.get_rank());
    payload.set_hole_top(cid);
    zobrist_hash_value ^= zobrist_hash::hole_top_key(cid);
}
```

When the hole is empty, `payload.set_hole_top()` is never called, so `hole_top = 0` (from `clear()`). The hash includes `zobrist_hash::hole_top_key(0)` implicitly (since the payload byte defaults to 0).

After pile undo:
- Hole non-empty: read `zobrist_hash::card_id(piles[hole].top_card()...)` → this is the old hole_top cid
- Hole empty: old hole_top = 0 (the default/empty sentinel)

**Verdict: WORKS** — hole top is readable from restored pile state. When hole is empty after undo, use 0 (the byte's default cleared value).

### Component 5: Waste Pointer

**Question:** Can we recover the pre-move waste pointer from restored pile state?

**Analysis:**

The waste pointer is computed by `effective_waste_ptr()`:
```cpp
uint8_t game_state::effective_waste_ptr() const {
    bool waste_deal_symmetry = rules.stock_redeal
        && piles[waste].size() % rules.stock_deal_count == 0;
    if (waste_deal_symmetry) return 0;
    return static_cast<uint8_t>(piles[waste].size());
}
```

This depends solely on `piles[waste].size()` and game rules. After pile undo, the waste pile is restored to its pre-move state, so `effective_waste_ptr()` returns the pre-move value.

**For `undo_regular_move`:** only relevant when `m.from == waste`. After `place_card(m.from, take_card(m.to))` the waste pile has the card back, so `effective_waste_ptr()` gives the old value.

**For `undo_stock_k_plus_move`:** This is more complex because pile operations include stock-to-waste transfers, a play from waste, and possibly a flip-waste. All must be reversed in exact order before `effective_waste_ptr()` is called. The current code already reverses pile ops in the correct order (lines 778–795). After full reversal, `effective_waste_ptr()` returns the pre-move value.

**Verdict: WORKS** — `effective_waste_ptr()` is a pure function of the waste pile size and game rules.

### Component 6: Stock-to-All-Tableau Card Descriptors

**Question:** Can we recover the pre-move descriptors for cards dealt from stock to tableau?

**Analysis:**

In `make_stock_to_all_tableau_move`, each card dealt from stock to a tableau pile gets a new descriptor via `determine_destination_descriptor(tab_pr, dealt)`. The old descriptor for each card was **STARTING** (they were in the stock, which always has STARTING).

The undo currently iterates in reverse and sets each card back to STARTING:
```cpp
update_card_descriptor(cid, compact_state::STARTING);
place_card(stock, take_card(tab_pr));
```

In a pile-first undo, we'd reverse the pile ops first (return all cards to stock), then set descriptors. After pile undo, all dealt cards are back in the stock. Their descriptors should be STARTING (stock cards are always STARTING).

**Verdict: WORKS** — stock cards always have STARTING descriptor. No ambiguity.

**Note:** The current undo uses `undo.sat_count` (from the undo record) to know how many cards were dealt. This count equals `m.count` from the original move — it IS stored in the move struct. So `sat_count` does NOT need the undo stack; it's available from the move itself.

---

## The Critical Question: ROOT vs IN_SPACE vs STARTING_FACE_UP

This is where we must be most careful, because the ROOT/IN_SPACE distinction was the subject of a correctness bug (false positives in the flat cache).

### Scenario: Card moved FROM tableau back to tableau

After pile undo, the card is back at `m.from`. We call `determine_destination_descriptor(m.from, card)`.

The function checks:
1. Is `piles[m.from].size() == 1` (card is alone on pile)? → **IN_SPACE**
2. Is there a card below at `piles[m.from][1]`? → Lookup parent table
   - Legal build parent found → **PARENT_0..3**
   - No legal build parent → **ROOT**

This correctly distinguishes IN_SPACE from ROOT:
- IN_SPACE: card is at bottom of pile with no card below (pile undo restored it to a pile that now has just this one card)
- ROOT: card is on a non-legal-build parent (pile undo restored it on top of whatever was below it before)

**But what if the original descriptor was STARTING_FACE_UP?**

STARTING_FACE_UP is assigned when a face-down card is revealed. The revealed card doesn't move — it stays in place and gets STARTING_FACE_UP (if there are cards below) or IN_SPACE (if it's the bottom card).

Consider this sequence:
1. Initial state: pile has [A, face-down-B] (A on top, B face-down below)
2. Move A somewhere (reveal_move=true): B turns face-up, gets STARTING_FACE_UP
3. Later moves happen to other piles
4. Move B somewhere: `old_desc` for B was STARTING_FACE_UP
5. Undo move B: need to restore B's descriptor to STARTING_FACE_UP

After pile undo, B is back at its source pile. What does `determine_destination_descriptor(m.from, B)` return?

- If B is alone on the pile (nothing below): **IN_SPACE** — but old_desc was STARTING_FACE_UP!
- If B has a card below: check parent table → **PARENT_x** or **ROOT** — but old_desc was STARTING_FACE_UP!

### **THIS IS A POSSIBLE BUG: STARTING_FACE_UP IS NOT RECOVERABLE FROM `determine_destination_descriptor`**

`determine_destination_descriptor` does not know whether a card is in a "revealed but unmoved" state. It always returns a positional descriptor (IN_SPACE, ROOT, PARENT_x, etc.) based on the card's current pile context. It never returns STARTING_FACE_UP.

STARTING_FACE_UP is a **history-dependent** descriptor: it means "this card was face-down and was revealed without being moved." Once the card is moved, its descriptor changes to something positional. But if we're undoing a move of this card back to where it came from (its revealed position), `determine_destination_descriptor` gives us the positional descriptor, not STARTING_FACE_UP.

### Is This Actually a Problem?

Let's think about when a card with STARTING_FACE_UP would be moved:

1. Card B is face-down in a tableau pile
2. Card A above B is moved away; B is revealed → B gets STARTING_FACE_UP
3. Card B is now face-up and can be moved to another tableau pile, foundation, cell, hole, etc.
4. When B is moved, `make_move` captures `old_desc = STARTING_FACE_UP`
5. After moving B, B gets a new descriptor based on its destination

When undoing step 4, we need to restore B to STARTING_FACE_UP.

**But what does `determine_destination_descriptor(m.from, B)` return after pile undo?**

After pile undo, B is back at `m.from`:
- The pile state below B is identical to what it was at step 3 (DFS undo is LIFO)
- Below B there are face-down cards (the same ones that were there when B was revealed)
- `determine_destination_descriptor` checks: pile not empty, card has a parent below...

Wait. The card below B is **face-down**. What happens when `determine_destination_descriptor` sees a face-down card below?

Looking at the code (line 1207):
```cpp
card parent_card = piles[dest][1];
```

It reads the card at index 1. If that card is face-down, it still has a suit and rank (face-down cards have identity). But the parent_table lookup is based on build policy — it checks if `parent_card` is a legal build parent of `moved_card`. A face-down card could be anything, and its build relationship is coincidental.

So the function would return:
- PARENT_x if the face-down card happens to be a legal build parent (coincidence!)
- ROOT if it's not a legal build parent
- Neither of these is STARTING_FACE_UP

**This IS a real discrepancy.** The correct descriptor is STARTING_FACE_UP, but `determine_destination_descriptor` returns ROOT or PARENT_x.

### How Severe Is This?

If we assign the wrong descriptor:
- The Zobrist hash will differ from what `make_move` computed
- The compact_state payload will differ
- The cache will think different states are the same (false positives) or same states are different (false negatives)
- This is a correctness bug of exactly the kind that caused the ROOT/IN_SPACE false positive issue

### What About IN_SPACE vs STARTING_FACE_UP?

If B is the bottom card of the pile (only B remains after A is moved), the reveal sets descriptor to IN_SPACE (not STARTING_FACE_UP). See make_regular_move line 508:
```cpp
uint8_t rev_desc = (piles[m.from].size() == 1)
    ? compact_state::IN_SPACE
    : compact_state::STARTING_FACE_UP;
```

In this case, `determine_destination_descriptor` also returns IN_SPACE for a card alone on a pile. So **bottom-of-pile reveals are fine**.

The problem is **only** when the revealed card has face-down cards below it, and it gets STARTING_FACE_UP. In this case, `determine_destination_descriptor` would examine the face-down card below and return ROOT or PARENT_x instead.

### How Often Does This Occur?

A card has STARTING_FACE_UP when:
1. It was revealed (face-down card turned face-up when card above is moved)
2. It has face-down cards below it (pile size > 1 after the card above was removed)
3. It has NOT been subsequently moved (if moved, it gets a new positional descriptor)

This card can then be moved by the solver as a normal tableau move. When undoing that move, we need STARTING_FACE_UP back.

This is common in Klondike-style games with face-down cards in the tableau. It would occur frequently.

---

## Resolution: STARTING_FACE_UP IS Recoverable

### The Key Invariant

`check_face_down_consistent()` enforces that **the top card of every non-empty tableau pile is face-up**. This is not just a debug assertion — it is a structural invariant of the game state:

```cpp
// From game_state.cpp:
assert(!piles[p].top_card().is_face_down());
// face-down cards never above face-up ones
```

This means: **you can never place a card on top of a face-down card**. When a move is made to a tableau pile, the pile's top card is face-up (or the pile is empty). The moved card is placed on top of that face-up card (or on an empty pile).

### The Proof

The only way a face-up card can sit directly above a face-down card is through a **reveal**: the card above was moved away, and this card (previously face-down) was turned face-up in place. It was never moved — it was revealed.

**Therefore, at undo time (after pile undo + reveal undo), if `piles[m.from][1].is_face_down()`, the moved card at `piles[m.from][0]` was REVEALED here, not MOVED here.** Its descriptor was:
- **STARTING_FACE_UP** (if there are face-down cards below it, i.e., pile size > 1 at the time)
- **IN_SPACE** (if it's the bottom card — but this can't happen with a face-down card at index 1)

Since we already know that IN_SPACE is recoverable (pile size == 1 after undo means the card was alone on the pile), the remaining case is:

> After pile undo + reveal undo: if `piles[m.from].size() >= 2 && piles[m.from][1].is_face_down()`, then `old_desc = STARTING_FACE_UP`.

Otherwise, `old_desc = determine_destination_descriptor(m.from, moved)`.

### The Complete Descriptor Reconstruction Rule

```cpp
uint8_t compute_old_descriptor(pile::ref from, card moved) {
    // After pile undo (and reveal undo if applicable), the card is at piles[from][0].

    // Check if the card was revealed here (face-down card below it)
    if (piles[from].size() >= 2 && piles[from][1].is_face_down()) {
        return compact_state::STARTING_FACE_UP;
    }

    // Otherwise, compute from pile context (handles IN_SPACE, ROOT, PARENT_x, etc.)
    return determine_destination_descriptor(from, moved);
}
```

### Why This Is Complete

| Pile State After Undo | Card Was... | Descriptor | Method |
|---|---|---|---|
| Pile size == 1 | Alone on pile (bottom) | IN_SPACE | `determine_destination_descriptor` |
| Index 1 is face-down | Revealed in place | STARTING_FACE_UP | Face-down check |
| Index 1 is face-up, legal parent | Moved onto legal parent | PARENT_0..3 | `determine_destination_descriptor` |
| Index 1 is face-up, non-legal parent | Moved onto non-legal parent | ROOT | `determine_destination_descriptor` |
| Foundation/cell/hole/etc. | In non-tableau pile | STARTING/IN_CELL/IN_HOLE | `determine_destination_descriptor` |

Every case is covered. No stored state is needed.

### What About Built-Group Moves?

For built-group moves, the bottom card of the group is the one whose descriptor changes. After pile undo, it's at `piles[m.from][m.count - 1]`. The card below it is at `piles[m.from][m.count]`. The same face-down check applies:

```cpp
if (piles[from].size() > m.count && piles[from][m.count].is_face_down()) {
    return compact_state::STARTING_FACE_UP;
}
```

### Conclusion: No Undo Stack Needed At All

With this face-down check, ALL descriptor values are recoverable from pile state. The 1-byte `descriptor_undo_stack` proposed earlier is **not needed**. The undo stack can be eliminated entirely.

---

## Summary (REVISED)

| Component | Recoverable from pile state? | Notes |
|---|---|---|
| Moved card descriptor | **YES** — all cases including STARTING_FACE_UP | Face-down invariant resolves the ambiguity |
| Revealed card identity | YES | Read from `piles[m.from][1]` (or `[m.count]` for built groups) |
| Revealed card old descriptor | YES | Always STARTING (face-down cards) |
| Foundation top rank | YES | Read pile top after undo |
| Hole top card | YES | Read pile top after undo (0 if empty) |
| Waste pointer | YES | Call `effective_waste_ptr()` after pile undo |
| sat_count | YES | Available from `move.count` |

**Bottom line:** The entire `zobrist_undo_stack` can be eliminated. ALL values are recoverable from pile state after undo. The key insight is the face-down card invariant: a face-up card sitting directly above a face-down card was necessarily revealed in place (descriptor = STARTING_FACE_UP), never moved there, because moves can only place cards on face-up tops or empty piles.
