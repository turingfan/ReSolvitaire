# Phase 1 Status: Pile-First Undo — Pickup Document

**Date written:** 2026-04-11
**Branch:** `feature/pile-first-undo`
**Last good commit:** `52b8668` — "feat: add recover_pre_move_descriptor helper (Phase 1 Step A)"
**Status:** BLOCKED on one bug in `recover_pre_move_descriptor`

---

## PROCESS RULE — READ FIRST

**When a bug is discovered during implementation, STOP and report it to Ian immediately. Do NOT spend time investigating or attempting to fix it. Describe what failed, what the symptom is, and ask Ian how to proceed. Do not use up session context on silent investigation.**

---

## What We Are Trying to Do

Eliminate the `zobrist_undo_stack` from `game_state`. Instead of storing old hash/payload values on a stack during `make_*` moves and restoring them during `undo_*` moves, we recover all values from the restored pile state after the pile operations run.

The full plan is in `/Users/ipg/.claude/plans/dazzling-gathering-thacker.md`.

The plan has 6 commits: A (helper), B (undo_regular_move), C (undo_built_group_move), D (undo_stock_k_plus_move), E (undo_stock_to_all_tableau_move), F (final cleanup).

Commit A is done. Commit B is partially implemented but **fails one test** due to a bug described below.

---

## Current State of the Code

### What is committed (`52b8668`):
- `recover_pre_move_descriptor` helper added to `game_state.cpp` and declared in `game_state.h`
- No other behavioral changes

### What is UNCOMMITTED (sitting in working tree on `feature/pile-first-undo`):
- `make_regular_move`: push to `zobrist_undo_stack` is guarded with `#ifdef VALIDATE_INLINE_UNDO`
- `undo_regular_move`: fully rewritten with pile-first approach (but has the bug below)
- `undo_built_group_move`, `undo_stock_k_plus_move`, `undo_stock_to_all_tableau_move`: NOT yet rewritten

The uncommitted `undo_regular_move` code structure is:
1. Record `pre_hash` / `pre_payload` (inside `#ifdef VALIDATE_INLINE_UNDO`)
2. Identify moved card from `piles[m.to].top_card()` before pile ops
3. If `m.reveal_move`: turn `piles[m.from][0]` face-down (reveal undo)
4. `place_card(m.from, take_card(m.to))` — return card to source
5. If `m.reveal_move`: update `piles[m.from][1]` card's descriptor to STARTING
6. Update foundation hash if `m.to` or `m.from` is a foundation
7. Update hole top hash if `m.to == hole`
8. Update waste ptr hash if `m.from == waste`
9. **Call `recover_pre_move_descriptor(m.from, moved)` — THIS HAS THE BUG**
10. `update_card_descriptor(cid, old_desc)`
11. Inside `#ifdef VALIDATE_INLINE_UNDO`: pop undo record, run reference path, assert match

---

## The Bug

### Symptom
Test `ZobristIncremental.KlondikeSeedMakeUndo` fails when built with `-DVALIDATE_INLINE_UNDO=ON`.

Assertion failure:
```
Assertion failed: (zobrist_hash_value == recovery_hash && "PILE RECOVERY: hash mismatch in undo_regular_move")
```

The pile-first path returns `old_desc = 1 = STARTING_FACE_UP` but the reference undo-stack path returns `old_desc = 0 = STARTING` for a specific card.

### Root Cause

`recover_pre_move_descriptor` uses the "face-down invariant" from `descriptor_undo_analysis.md` to decide if a card's old descriptor was STARTING_FACE_UP:

```cpp
uint8_t game_state::recover_pre_move_descriptor(pile::ref from, card moved_card) const {
    if (!original_tableau_piles.empty()) {
        pile::ref first_tab = original_tableau_piles.front();
        pile::ref last_tab = original_tableau_piles.back();
        if (from >= first_tab && from <= last_tab
            && piles[from].size() >= 2 && piles[from][1].is_face_down()) {
            return compact_state::STARTING_FACE_UP;  // ← BUG: wrong for some cards
        }
    }
    return determine_destination_descriptor(from, moved_card);
}
```

The invariant says: "if there is a face-down card at `pile[from][1]` after pile undo, the returned card was revealed in place, so its old descriptor was STARTING_FACE_UP."

**This invariant is WRONG.** Here is a concrete counterexample from Klondike seed 42:

- CID 28 (3 of Hearts) is dealt into a Klondike tableau pile (pile 10) face-down as part of the initial diagonal deal
- `init_payload_and_hash()` runs **before** `piles[pr][0].turn_face_up()` is called (the top card face-up step at lines 303–307)
- So at `init_payload_and_hash` time, CID 28 is at `pile[0]` and is face-DOWN → assigned descriptor STARTING=0
- After init, CID 28 is turned face-up (it IS the top card of pile 10)
- During play, CID 28 is the card that gets moved with `m.reveal_move=true` (it reveals the card below it when moved)
- When we undo this move and pile undo runs, `piles[m.from][1].is_face_down()` is true (the card below is face-down)
- So `recover_pre_move_descriptor` returns STARTING_FACE_UP=1
- But the actual old descriptor was STARTING=0 (it was assigned STARTING at init time because it was face-down at that moment)

### Why the Invariant is Wrong

The invariant assumes that any face-up card that now sits above a face-down card must have been **revealed during play** (and therefore has STARTING_FACE_UP descriptor). But cards that are the **initial top card** of a Klondike tableau pile are also face-up above face-down cards — and they have STARTING=0 because they were face-down when `init_payload_and_hash` scanned them.

In summary: **`init_payload_and_hash` runs before `turn_face_up()` in the seed-based constructor**, so initially-top-face-up cards are assigned STARTING=0, not STARTING_FACE_UP=1.

---

## What Needs to Be Fixed

The `recover_pre_move_descriptor` function cannot distinguish:
- A card revealed **during play** (descriptor = STARTING_FACE_UP = 1)
- A card that was the **initial top card** of a Klondike pile (descriptor = STARTING = 0)

Both satisfy `piles[from][1].is_face_down()` after undo.

### Option A: Store 1 byte per move (minimal approach)
Add a single byte `old_moved_desc` to the existing `zobrist_undo` struct (or a separate tiny struct), storing only the moved card's old descriptor. Everything else (foundations, hole, waste ptr, reveal card) is recoverable from pile state — only the moved card's original descriptor is not. This eliminates 9 of the 10 bytes per move, reduces the undo record from `zobrist_undo` (10 bytes) to 1 byte.

### Option B: Fix the init ordering
Change the seed-based constructor so `init_payload_and_hash()` is called **after** `turn_face_up()`. Then initially-top-face-up cards would be assigned STARTING_FACE_UP=1 at init time, making the invariant correct. **However**: this would change the descriptor semantics for initial top cards and may break the flat cache or other tests. Needs careful analysis.

### Option C: Track revealed cards separately
Store which cards have ever been revealed (e.g. a 52-bit bitmask), so `recover_pre_move_descriptor` can consult this. More complex.

**Recommendation: Option A** — it's the least invasive, provably correct, and reduces undo storage by ~90%.

---

## Files to Read When Starting Work

1. **`docs/refactoring/phase1_plan.md`** — the detailed plan (note: plan was designed assuming the invariant was correct; needs updating for Option A)
2. **`/Users/ipg/.claude/plans/dazzling-gathering-thacker.md`** — plan in Claude's memory, same issue
3. **`src/main/game/search-state/game_state.cpp`** — the implementation (current state has uncommitted changes)
4. **`src/main/game/search-state/game_state.h`** — `recover_pre_move_descriptor` declaration is there
5. **`docs/refactoring/descriptor_undo_analysis.md`** — contains the flawed proof; Section "Resolution: STARTING_FACE_UP IS Recoverable" is incorrect

---

## Build Commands for Testing

```bash
# From repo root
./build.sh --debug --unit-tests  # debug build with unit tests
cd cmake-build-debug

# Build with validation flag
cmake -DVALIDATE_INLINE_UNDO=ON .. && make -j4

# Run the specific failing test
./bin/unit_tests --gtest_filter="ZobristIncremental.KlondikeSeedMakeUndo"

# Run all Zobrist tests
./bin/unit_tests --gtest_filter="ZobristIncremental.*"

# Run full unit tests
./bin/unit_tests
```

---

## Summary of Decision Needed

Before writing any more code, decide: **Option A (1-byte old_desc per move) or Option B (fix init ordering) or Option C (tracking set)?**

Option A is recommended. It means:
- The `zobrist_undo` struct shrinks to just `uint8_t old_moved_desc`
- All other undo values (reveal, foundations, hole, waste) are recovered from pile state
- The push in `make_regular_move` stores only 1 byte
- The `recover_pre_move_descriptor` function is deleted
- `undo_regular_move` pops that 1 byte and uses it directly
