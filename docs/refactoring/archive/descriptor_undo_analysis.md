# Descriptor Undo Reconstruction: Semantic Analysis

**Date:** 2026-04-10 (revised 2026-04-11, 2026-04-13)
**Status:** IMPLEMENTED — Commit B on `feature/pile-first-undo` implements the corrected approach; see "Corrected Resolution" and "Implementation Notes (Commit B)"

---

## The Question

When undoing a move using the "pile-first" approach (reverse pile operations, then recompute descriptor values from restored pile state), can we always **uniquely determine the correct pre-move descriptor** for each card affected?

This document analyses every case systematically to identify where reconstruction works, where it fails, and what (if anything) needs to be stored to make it work.

---

## Descriptor Vocabulary

From `compact_state.h`:

| Value | Name | Meaning |
|---|---|---|
| 0 | STARTING | Face-down card; card in stock/waste/reserve/foundation; OR initially-face-up card in a multi-card initial tableau pile (counterintuitive — see KI-2) |
| 1 | STARTING_FACE_UP | Card that was face-down at deal and was revealed during play (NOT at bottom of pile) |
| 2 | ROOT | Card on a non-legal-build parent in the tableau |
| 3 | IN_CELL | Card in a cell pile |
| 4–7 | PARENT_0–3 | Card on a legal-build parent (index identifies which parent) |
| 8 | IN_HOLE | Card in the hole pile |
| 9 | IN_SPACE | Card placed in an empty tableau pile; also the top card of an initial single-card tableau pile |

Key semantic distinctions:
- **STARTING vs STARTING_FACE_UP**: STARTING covers face-down cards, non-tableau piles, AND initially-face-up cards in multi-card tableau piles (because `init_payload_and_hash` runs before `turn_face_up()` in the seed constructor — those cards are still face-down at init time). STARTING_FACE_UP is reserved for cards that start face-down and are later revealed during play.
- **STARTING_FACE_UP vs IN_SPACE on reveal**: When a card above is moved away, the revealed card gets IN_SPACE if it is now alone on the pile, or STARTING_FACE_UP if face-down cards remain below it.
- **ROOT vs IN_SPACE**: Both appear at/near the bottom of a tableau pile. IN_SPACE means nothing is below (empty pile); ROOT means a non-legal-build card is below.
- The ROOT/IN_SPACE distinction was the subject of a critical bug fix (see `bug_report_root_descriptor_false_positives.md`).
- **KI-2 (naming anomaly)**: STARTING(0) is assigned to cards that START face-up; STARTING_FACE_UP(1) is for cards that START face-down and are revealed. The names are backwards. Renaming deferred.

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

## Resolution: STARTING_FACE_UP IS Recoverable (FLAWED — see Corrected Resolution below)

> **Note:** The proof in this section is incorrect. The face-down invariant fails for initially-face-up cards. The section is preserved for context. See "Corrected Resolution" for the valid approach.

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

### Conclusion of Flawed Section

The face-down check was believed to cover all cases, but it does not. See Corrected Resolution below.

---

## Corrected Resolution: Static Initial State Lookup

### The Counterexample

The face-down invariant claims: "a face-up card sitting directly above a face-down card was necessarily revealed during play." This is wrong for the **initial top card** of a Klondike tableau pile.

Concrete example (Klondike seed 42, CID 28 = 3 of Hearts):
- CID 28 is dealt as the top card of pile 10, which in Klondike means it is face-up in the logical initial state
- However, `init_payload_and_hash()` runs **before** `turn_face_up()` in the constructor
- At `init_payload_and_hash` time, CID 28 is still face-down → assigned `STARTING=0`
- After init, `turn_face_up()` turns CID 28 face-up
- During play, CID 28 is moved (with `m.reveal_move=true`, revealing the card below)
- On undo, `piles[m.from][1].is_face_down()` is true — the face-down card below is visible
- The flawed heuristic returns `STARTING_FACE_UP=1`, but the correct old descriptor is `STARTING=0`

The invariant fails because it cannot distinguish "initially face-up above a face-down card" from "revealed during play above a face-down card."

### The Correct Fix: Static Initial State

Add `bool initially_face_up[52]` to `game_state`, populated **after** `turn_face_up()` runs in the constructor. This records the logical initial face-up/face-down state for each card, indexed by CID.

The rule for recovering the moved card's old descriptor when it is returning to its starting tableau pile:

```cpp
if (initially_face_up[cid]) {
    return compact_state::STARTING;        // was face-up at start → got STARTING=0 at init
} else {
    return compact_state::STARTING_FACE_UP; // was face-down at start → revealed during play
}
```

**Why this is correct:**
- A logically face-up card at the start was face-down when `init_payload_and_hash` ran → assigned `STARTING=0`. Its old descriptor is always `STARTING=0`.
- A logically face-down card at the start was assigned `STARTING=0` at init. It only acquires `STARTING_FACE_UP=1` after being revealed during play. When it is subsequently moved, its old descriptor is `STARTING_FACE_UP=1`.

This approach requires no per-move storage and replaces `recover_pre_move_descriptor` entirely.

**Limitation — 2-deck games:** `initially_face_up[52]` is indexed by CID. In 2-deck games, two physical copies of the same card share a CID. The lookup is ambiguous and may be incorrect. This is a known issue — see PICKUP.md KI-1.

**Note on naming:** Cards that start face-up receive `STARTING=0`; cards that start face-down and are later revealed receive `STARTING_FACE_UP=1`. The names are counterintuitive. This is a known issue — see PICKUP.md KI-2.

---

## Implementation Notes (Commit B — 2026-04-13)

Commit B on `feature/pile-first-undo` implements the corrected approach for `undo_regular_move`. Two additional semantic fixes were discovered during implementation.

### Additional Semantic Fix 1: Face-Up Card Above Face-Down Parent at Init

`init_payload_and_hash()` had a bug: in init-list and JSON constructors, a face-up card sitting directly above a face-down card in the initial tableau got `PARENT_x` (from the parent table lookup on the face-down card below it). The correct descriptor is `STARTING(0)`.

**Rationale:** A face-down card is not a legal move target. `determine_destination_descriptor` is only ever called with face-up destinations (you cannot make a `make_regular_move` to a face-down card). Therefore only the constructors can create a "face-up above face-down" position — and the correct descriptor at that point is `STARTING(0)`, not `PARENT_x`.

**Fix:** In the positional loop in `init_payload_and_hash()`, added:
```cpp
card parent_card = p[i + 1];
if (parent_card.is_face_down()) continue;  // keep STARTING=0, no build relationship visible
```

### Additional Semantic Fix 2: Initially-Face-Up Single-Card Tableau Pile → IN_SPACE

In the seed constructor, `init_payload_and_hash()` runs before `turn_face_up()`. A card that is the sole occupant of an initial tableau pile (e.g. the top card of each Klondike pile) is face-down at init time and gets `STARTING=0`. After `turn_face_up()`, it is face-up. The semantically correct descriptor is `IN_SPACE(9)` — it is "placed in an empty space" and `determine_destination_descriptor` returns IN_SPACE for a single-card pile.

**Fix:** In `init_initially_face_up()`, after scanning all piles, a fixup pass over `original_tableau_piles` updates any single-card face-up pile top from `STARTING=0` to `IN_SPACE=9`:
```cpp
for (auto tab_ref : original_tableau_piles) {
    if (piles[tab_ref].size() == 1 && !piles[tab_ref][0].is_face_down()) {
        card c = piles[tab_ref][0];
        uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
        if (payload.get_descriptor(cid) == compact_state::STARTING) {
            update_card_descriptor(cid, compact_state::IN_SPACE);
        }
    }
}
```

This fixup is skipped for accordion/predecessor-cache games (guarded with `if (uses_predecessor_cache()) return;`).

### Actual Implementation of the Pile-First Rule in undo_regular_move

The implemented rule is more precise than the sketch above. It only applies the static lookup when `m.from` is within the original tableau pile range AND there is a face-down card immediately below the returned card:

```cpp
uint8_t old_desc = determine_destination_descriptor(m.from, moved);  // default
if (!original_tableau_piles.empty()) {
    pile::ref first_tab = original_tableau_piles.front();
    pile::ref last_tab  = original_tableau_piles.back();
    if (m.from >= first_tab && m.from <= last_tab
            && piles[m.from].size() >= 2 && piles[m.from][1].is_face_down()) {
        old_desc = initially_face_up[cid]
            ? compact_state::STARTING          // was face-up at start → STARTING=0
            : compact_state::STARTING_FACE_UP; // was face-down at start → revealed
    }
}
update_card_descriptor(cid, old_desc);
```

The range check (`m.from >= first_tab && m.from <= last_tab`) restricts the lookup to original tableau piles. Non-original piles (cells, waste, etc.) are handled entirely by `determine_destination_descriptor`. Single-card piles (size == 1) are also handled by `determine_destination_descriptor`, which returns `IN_SPACE` — correct after the init fixup above.

### VALIDATE_INLINE_UNDO

When compiled with `-DVALIDATE_INLINE_UNDO=ON`, `make_regular_move` still pushes to `zobrist_undo_stack` (guarded), and `undo_regular_move` runs both the pile-first path and the reference undo-stack path, asserting they agree. A `log_pile_recovery_mismatch()` debug helper fires only on disagreement and dumps the full 52-card descriptor diff. All 24 ZobristIncremental + FaceUpCards tests pass under this flag.

---

## Summary (REVISED — corrected 2026-04-11, implemented 2026-04-13)

| Component | Recoverable from pile state? | Notes |
|---|---|---|
| Moved card descriptor | **YES** — static lookup + positional fallback | `initially_face_up[cid]` applied only when pile[1] is face-down in orig tableau; `determine_destination_descriptor` handles all other cases |
| Revealed card identity | YES | Read from `piles[m.from][1]` (or `[m.count]` for built groups) |
| Revealed card old descriptor | YES | Always STARTING (face-down cards) |
| Foundation top rank | YES | Read pile top after undo |
| Hole top card | YES | Read pile top after undo (0 if empty) |
| Waste pointer | YES | Call `effective_waste_ptr()` after pile undo |
| sat_count | YES | Available from `move.count` |

**Two additional semantic fixes were required** beyond the corrected heuristic:
- `init_payload_and_hash`: face-up above face-down parent → STARTING(0), not PARENT_x
- `init_initially_face_up`: single-card initially-face-up pile → IN_SPACE(9) fixup

**Bottom line:** The entire `zobrist_undo_stack` can be eliminated. All values are recoverable — but the moved card's descriptor requires a static lookup (`initially_face_up[cid]` stored in `game_state`), plus two init-time semantic fixes to keep the reference path consistent.
