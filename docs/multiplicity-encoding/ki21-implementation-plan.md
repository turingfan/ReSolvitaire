# KI-21 Implementation Plan: Waste Descriptor O(stock) → O(1)

## Problem (from known-issues.md #21)

`MLD_IN_WASTE` was assigned to ALL waste cards. A `stock_k_plus` move with
`count=k` changes k waste cards' descriptors, making the incremental update O(k)
— defeating the purpose of incremental updates.

## Fix

Collapse `MLD_IN_WASTE` to the single top-of-waste card only. All other waste
cards use `MLD_IN_STOCK` (same as stock cards — they are unreachable until
promoted to top). This is the same insight as `waste_deal_sym` already applied:
the encoding cannot distinguish them from stock.

A `stock_k_plus` move now changes at most 3 descriptors:
- Played card → descriptor at `m.to`
- Old waste top → `MLD_IN_STOCK` (if it moved away from pos 0)
- New waste top → `MLD_IN_WASTE` (if it differs from old top)

## Files Changed

1. `src/main/game/multiplicity_descriptor_engine.h` — `recompute_all()` waste block
2. `src/main/game/search-state/game_state.cpp` — `mult_desc_at()`, `make_move()`, `undo_move()`
3. `src/test/unit_tests/multiplicity_incremental_test.cpp` — new stock_k_plus tests

## Part 1: Descriptor Semantics Change

### recompute_all() (~line 371-378)

Before: all waste cards → `waste_deal_sym ? MLD_IN_STOCK : MLD_IN_WASTE`
After:  waste[0] → `MLD_IN_WASTE` (if !waste_deal_sym); waste[i>0] → `MLD_IN_STOCK`

### mult_desc_at() (~line 1368-1373)

Before: all waste cards → `waste_deal_sym ? MLD_IN_STOCK : MLD_IN_WASTE`
After:  pos==0 → `MLD_IN_WASTE` (if !waste_deal_sym); pos>0 → `MLD_IN_STOCK`

Part 1 alone passes all tests: `stock_k_plus` still uses `recompute_all()` fallback,
and `verify_against_scratch()` checks `mult_desc_at()` agrees with `recompute_all()`.

## Part 2: Incremental stock_k_plus

### Pre-move capture in make_move (before outer switch)

Capture waste top card ID before pile operations.

### make_move multiplicity switch

Replace `stock_k_plus` fallback with O(1) incremental:
1. `played_cid` → `mult_desc_at(m.to, 0)`
2. If old waste top moved (≠ new waste top): old_top_cid → `MLD_IN_STOCK`
3. If new waste top changed (≠ old waste top): new_top_cid → `mult_desc_at(waste, 0)`

### Pre-undo capture in undo_move (before outer switch)

Capture played card (at `m.to`) and waste top before pile operations.

### undo_move multiplicity switch

Replace `stock_k_plus` fallback with O(1) incremental:
1. `pre_undo_played_cid` → `MLD_IN_STOCK` (card returns to stock/waste interior)
2. Post-undo waste top → `mult_desc_at(waste, 0)` (new top of waste)
3. If pre-undo waste top changed (≠ post-undo top): pre_undo_top_cid → `MLD_IN_STOCK`

### stock_to_all_tableau

Kept as `mult_fallback = true` — not in scope.

## Domain Questions Log

(Empty — no unresolved questions during implementation.)
