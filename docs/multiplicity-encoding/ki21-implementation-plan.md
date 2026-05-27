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

### Bug: count=0 overwrite in make_move (found in PR #4 review)

`generate_k_plus_moves_to_check()` inserts count=0 when waste is non-empty. When
count=0, `played_cid == pre_move_waste_top_cid` (waste top IS the played card). The
original implementation emitted `{pre_move_waste_top_cid, MLD_IN_STOCK}` which
overwrote the played card's correct descriptor set in changes[0].

Fix: add `&& pre_move_waste_top_cid != played_cid` guard to the old-waste-top
condition in make_move. Same guard added implicitly to undo_move by restructuring.

### Bug: undo_move count=0 fragile ordering

When count=0, after undo the played card returns to waste top, so its descriptor
should be `mult_desc_at(waste, 0)`, not `MLD_IN_STOCK`. Original code relied on
overwrite order (post-undo waste top entry would overwrite the MLD_IN_STOCK entry
for the same card). Fragile and incorrect for the cascade version.

Fix: compute post_undo_waste_top_cid first, then check whether played == post-undo
waste top and emit `mult_desc_at(waste,0)` or `MLD_IN_STOCK` accordingly.

### Bug: missing hole-top handling for stock_k_plus (found in PR #4 review)

`stock_k_plus` can target the hole (`add_stock_to_hole_foundation_moves`). The
`regular` case had `m.to == hole` logic for MLD_PERMANENT / mult_desc_at(hole,0);
the `stock_k_plus` case was missing both make and undo directions.

Fix: added hole-top blocks to both make_move and undo_move stock_k_plus cases,
mirroring the pattern from the regular case.

### Trace test status (after bug fixes)

After applying the bug fixes, `trace_mult_vs_flat_klondike` and
`trace_mult_vs_flat_canfield` were restored to mult-vs-flat comparisons and
both PASS. The pre-fix divergence (operation ~75 in klondike) was caused by the
count=0 bug producing an incorrect descriptor for the played card, not by a
fundamental incompatibility between multiplicity and flat hashing. With correct
descriptors, both caches agree on every HIT/MISS/INSERT before the first
eviction for the tested seeds.
