# KI-21 Implementation Plan — Waste Descriptor O(stock) Fix

**Date:** 2026-05-25
**Branch:** Create `fix/ki21-waste-descriptor` from `multiplicity-encoding`
**Author:** Ian Gent with Claude (planning)
**Implementer:** Claude Code on the Web (Sonnet)

---

## Problem

The multiplicity encoding uses separate `MLD_IN_STOCK` and `MLD_IN_WASTE` locative
descriptors for stock and waste cards. When a `stock_k_plus` move deals k cards from
stock to waste, all k cards change descriptor from `MLD_IN_STOCK` to `MLD_IN_WASTE`.
This forces a full `recompute_all()` fallback (O(n_cards)) instead of an incremental
update (O(1)).

## Solution

Only the **top of waste** gets `MLD_IN_WASTE`. All other waste cards get `MLD_IN_STOCK`
(same as stock cards). This is correct because non-top waste cards are unreachable —
only the waste top can be played.

With this fix, a `stock_k_plus` move changes at most **3** multiplicity descriptors:
1. The played card (waste top after dealing → destination pile)
2. The old waste top before dealing (was `MLD_IN_WASTE` → now `MLD_IN_STOCK`)
3. The new waste top after playing (was `MLD_IN_STOCK` → now `MLD_IN_WASTE`)

Plus 1 optional change if `m.to == hole` (old hole top → `MLD_PERMANENT`).
Maximum 4 changes total, which fits the existing `mult_changes[4]` array.

## Two-Part Implementation

### Part 1: Descriptor Semantics Change

Change `MLD_IN_WASTE` to mean "top of waste only". Three locations:

#### 1a. `multiplicity_descriptor_engine.h` `recompute_all()` (~line 371-378)

Current waste section:
```cpp
if (ctx.waste != pile::ref(255)) {
    const pile& wp = ctx.piles[ctx.waste];
    uint8_t waste_loc = waste_deal_sym ? MLD_IN_STOCK : MLD_IN_WASTE;
    for (pile::size_type i = 0; i < wp.size(); i++) {
        card c = wp[i];
        descriptors[card_cid(c)] =
            multiplicity_descriptor::make_locative(waste_loc);
    }
}
```

Change to: only top card (`wp[0]`, which is `pile::top_card()`) gets `waste_loc`.
All other waste cards get `MLD_IN_STOCK`:
```cpp
if (ctx.waste != pile::ref(255)) {
    const pile& wp = ctx.piles[ctx.waste];
    for (pile::size_type i = 0; i < wp.size(); i++) {
        card c = wp[i];
        uint8_t loc = (i == 0 && !waste_deal_sym) ? MLD_IN_WASTE : MLD_IN_STOCK;
        descriptors[card_cid(c)] =
            multiplicity_descriptor::make_locative(loc);
    }
}
```

#### 1b. `game_state.cpp` `mult_desc_at()` (~line 1368-1373)

Current waste case:
```cpp
if (pr == waste) {
    bool waste_deal_sym = rules.stock_redeal
        && piles[waste].size() % rules.stock_deal_count == 0;
    return multiplicity_descriptor::make_locative(
        waste_deal_sym ? MLD_IN_STOCK : MLD_IN_WASTE, fd);
}
```

Change to: only position 0 (top) gets `MLD_IN_WASTE`:
```cpp
if (pr == waste) {
    if (pos == 0) {
        bool waste_deal_sym = rules.stock_redeal
            && piles[waste].size() % rules.stock_deal_count == 0;
        return multiplicity_descriptor::make_locative(
            waste_deal_sym ? MLD_IN_STOCK : MLD_IN_WASTE, fd);
    }
    return multiplicity_descriptor::make_locative(MLD_IN_STOCK, fd);
}
```

#### 1c. Verify `recompute_all` and `mult_desc_at` agree

The debug assertion `verify_against_scratch()` at `game_state.cpp:512` checks that
the incremental result matches `recompute_all()`. Both functions must use the same
descriptor semantics. After changing both 1a and 1b, this assertion validates consistency.

### Part 2: Incremental Updates for `stock_k_plus`

Replace `mult_fallback = true` with O(1) incremental change computation.

#### Pre-move state capture

Before the pile operations (before `make_stock_k_plus_move(m)` is called), capture
the old waste top. Add this **before** the switch statement at `game_state.cpp:413`:

```cpp
// Capture pre-move waste top for multiplicity incremental (stock_k_plus)
uint8_t old_waste_top_cid = 255;  // sentinel: no waste top
if constexpr (Policy::computes_multiplicity_descriptor) {
    if (m.type == move::mtype::stock_k_plus
        && waste != pile::ref(255) && !piles[waste].empty()) {
        card owt = piles[waste].top_card();
        old_waste_top_cid = zobrist_hash::card_id(owt.get_suit(), owt.get_rank());
    }
}
```

Similarly, add the same capture before `undo_move`'s switch at `game_state.cpp:522`.
For undo, the "old waste top" is the waste top in the forward-move state (before undoing).

#### Forward incremental (`make_move`, ~line 487-491)

Replace:
```cpp
case move::mtype::stock_k_plus:
case move::mtype::stock_to_all_tableau:
    // Complex moves: fall back to from-scratch
    mult_fallback = true;
    break;
```

With:
```cpp
case move::mtype::stock_k_plus: {
    // Played card: now at top of m.to
    uint8_t played_cid = zobrist_hash::card_id(
        piles[m.to].top_card().get_suit(),
        piles[m.to].top_card().get_rank());
    mult_changes[mult_n++] = {played_cid, mult_desc_at(m.to, 0)};

    // Old waste top: had MLD_IN_WASTE, now buried or in stock → MLD_IN_STOCK
    // (unless it's still the waste top, which happens when count <= 0
    //  and no flip — old waste top stays at waste top after undo-dealing)
    if (old_waste_top_cid != 255 && old_waste_top_cid != played_cid) {
        bool still_waste_top = !piles[waste].empty()
            && old_waste_top_cid == zobrist_hash::card_id(
                piles[waste].top_card().get_suit(),
                piles[waste].top_card().get_rank());
        if (still_waste_top) {
            // Descriptor may change due to waste_deal_sym condition changing
            mult_changes[mult_n++] = {old_waste_top_cid, mult_desc_at(waste, 0)};
        } else {
            mult_changes[mult_n++] = {old_waste_top_cid,
                multiplicity_descriptor::make_locative(MLD_IN_STOCK)};
        }
    }

    // New waste top (different from old waste top): was MLD_IN_STOCK → MLD_IN_WASTE
    if (!piles[waste].empty()) {
        uint8_t nwt_cid = zobrist_hash::card_id(
            piles[waste].top_card().get_suit(),
            piles[waste].top_card().get_rank());
        // Only emit if not already handled above
        if (nwt_cid != played_cid && nwt_cid != old_waste_top_cid) {
            mult_changes[mult_n++] = {nwt_cid, mult_desc_at(waste, 0)};
        }
    }

    // If moved to hole, old hole top becomes PERMANENT
    if (m.to == hole && piles[hole].size() > 1) {
        card old_top = piles[hole][1];
        uint8_t old_cid = zobrist_hash::card_id(
            old_top.get_suit(), old_top.get_rank());
        mult_changes[mult_n++] = {old_cid,
            multiplicity_descriptor::make_locative(MLD_PERMANENT)};
    }
    break;
}
case move::mtype::stock_to_all_tableau:
    // Deals to multiple piles — keep fallback
    mult_fallback = true;
    break;
```

#### Undo incremental (`undo_move`, ~line 598-601)

Same structure. After `undo_stock_k_plus_move()` completes, piles are back to
pre-forward-move state. The changes mirror the forward direction:

```cpp
case move::mtype::stock_k_plus: {
    // The card that was at m.to (played card) is now back in stock or waste
    // Find it: after undo, it's wherever it was before the forward move
    // We need its card ID — it was at m.to before undo
    // But undo already moved it. We captured old_waste_top_cid before undo.
    // Actually, for undo we need the pre-undo state of the played card.
    
    // APPROACH: After undo, piles are in original state. Just identify
    // which cards might have changed descriptors and provide current descriptors.
    
    // The waste top (restored): might need MLD_IN_WASTE
    if (!piles[waste].empty()) {
        card wt = piles[waste].top_card();
        uint8_t wt_cid = zobrist_hash::card_id(wt.get_suit(), wt.get_rank());
        mult_changes[mult_n++] = {wt_cid, mult_desc_at(waste, 0)};
    }

    // Old waste top before undo (= post-forward-move waste top)
    // This was MLD_IN_WASTE during the forward state; after undo it's somewhere
    // with possibly MLD_IN_STOCK
    if (old_waste_top_cid != 255) {
        bool is_current_waste_top = !piles[waste].empty()
            && old_waste_top_cid == zobrist_hash::card_id(
                piles[waste].top_card().get_suit(),
                piles[waste].top_card().get_rank());
        if (!is_current_waste_top) {
            mult_changes[mult_n++] = {old_waste_top_cid,
                multiplicity_descriptor::make_locative(MLD_IN_STOCK)};
        }
        // If it IS the current waste top, we already handled it above
    }

    // The played card: was at m.to with some descriptor, now in stock/waste
    // Find it at its restored position
    // ISSUE: We don't easily know the played card's ID after undo.
    // SOLUTION: Capture it before undo, just like old_waste_top_cid.
    // Add a pre-undo capture of the played card:
    //   uint8_t played_cid = card_id(piles[m.to].top_card());
    // Then after undo, it's in stock or waste with MLD_IN_STOCK
    // (or it might be the restored waste top — already handled above)
    if (played_cid != 255) {
        bool is_current_waste_top = !piles[waste].empty()
            && played_cid == zobrist_hash::card_id(
                piles[waste].top_card().get_suit(),
                piles[waste].top_card().get_rank());
        if (is_current_waste_top) {
            // Already handled above (or will be — ensure no duplicate)
        } else {
            mult_changes[mult_n++] = {played_cid,
                multiplicity_descriptor::make_locative(MLD_IN_STOCK)};
        }
    }

    // If m.to was hole, the hole top changes
    if (m.to == hole && !piles[hole].empty()) {
        card new_top = piles[hole][0];
        uint8_t top_cid = zobrist_hash::card_id(
            new_top.get_suit(), new_top.get_rank());
        mult_changes[mult_n++] = {top_cid, mult_desc_at(hole, 0)};
    }
    break;
}
case move::mtype::stock_to_all_tableau:
    mult_fallback = true;
    break;
```

**IMPORTANT:** The undo direction also needs pre-undo captures:
- `old_waste_top_cid`: waste top in the forward-move state (before undoing)
- `played_cid`: card at m.to (the played card, before it's moved back)

Add before the undo switch:
```cpp
uint8_t old_waste_top_cid = 255;
uint8_t undo_played_cid = 255;
if constexpr (Policy::computes_multiplicity_descriptor) {
    if (m.type == move::mtype::stock_k_plus) {
        if (waste != pile::ref(255) && !piles[waste].empty()) {
            card owt = piles[waste].top_card();
            old_waste_top_cid = zobrist_hash::card_id(owt.get_suit(), owt.get_rank());
        }
        card played = piles[m.to].top_card();
        undo_played_cid = zobrist_hash::card_id(played.get_suit(), played.get_rank());
    }
}
```

## Correctness Verification

### Debug assertion

`verify_against_scratch()` (line 512 of `game_state.cpp`) runs in debug builds after
every incremental update and compares against `recompute_all()`. This catches any
disagreement between the incremental logic and the from-scratch computation.

### Unit tests

Add tests to `multiplicity_incremental_test.cpp`:

1. **StockKPlusDeal1_IncrementalMatchesScratch**: klondike-deal-1, stock_k_plus with
   count=1. Verify incremental update produces same hash as recompute_all.

2. **StockKPlusDeal3_IncrementalMatchesScratch**: klondike with deal_count=3. Three
   cards dealt, played card to tableau. Verify hash agreement.

3. **StockKPlusToFoundation_IncrementalMatchesScratch**: Play from waste to foundation.
   Verify foundation descriptor update + waste descriptor changes.

4. **WasteDealSymmetry_StockKPlus**: klondike with redeal, waste size divisible by
   deal_count. Verify waste_deal_sym path produces correct descriptors.

5. **StockKPlusEmptyWaste**: Start with empty waste, deal from stock. Verify new waste
   top gets MLD_IN_WASTE.

### Existing test gates

All 3 test gates must pass:
```bash
python3 scripts/run_tests.py
```

The debug gate is particularly important — it runs `verify_against_scratch()` on
every move, which will catch incremental/from-scratch disagreement.

### Solvability cross-check

After tests pass, run solvability verification on stock/waste games:
```bash
# klondike seeds 1-20, multiplicity vs auto
for s in $(seq 1 20); do
    echo "Seed $s:"
    diff <(./cmake-build-release/bin/solvitaire --type klondike --random $s \
           --cache-type multiplicity --json 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin)['solution_type'])") \
         <(./cmake-build-release/bin/solvitaire --type klondike --random $s \
           --json 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin)['solution_type'])")
done
```

## Files Changed (Summary)

| File | What changes |
|---|---|
| `src/main/game/multiplicity_descriptor_engine.h` | `recompute_all()` waste section: top-only MLD_IN_WASTE |
| `src/main/game/search-state/game_state.cpp` | `mult_desc_at()`: pos==0 check for waste; `make_move()`/`undo_move()`: pre-move captures + stock_k_plus incremental logic |
| `src/test/unit_tests/multiplicity_incremental_test.cpp` | New unit tests for stock_k_plus incremental |

## What NOT to Change

- `MLD_IN_WASTE` enum value stays the same (value 3) — no new enum needed
- `stock_to_all_tableau` stays as `mult_fallback = true`
- The flat descriptor engine (`flat_descriptor_engine.h`) is unrelated — do not touch
- The existing `waste_deal_sym` logic in `recompute_all()` is correct and stays
- `effective_waste_ptr()` is unrelated — do not touch

## Risk Assessment

**Low risk.** The descriptor change is local to the multiplicity engine. The debug
assertion `verify_against_scratch()` provides a comprehensive safety net — any
incremental/from-scratch disagreement is caught immediately in debug builds.

The main subtlety is correctly identifying which cards' descriptors change during a
`stock_k_plus` move, especially with `flip_waste` (redeal) and negative `count`
(un-dealing). The implementation plan above handles all cases, but the implementer
should pay careful attention to the undo direction.

## Domain Questions Log

If the implementer encounters domain questions that cannot be resolved from code or
documentation, log them in this section rather than guessing:

*(To be filled by implementer)*
