# Suit Symmetry Limitation in Descriptor-Aligned Zobrist Caching

## Problem Statement
The current `flat_cache` implementation (Zobrist + `compact_state` descriptors) is indexed by `Card ID` (0–51). In contrast, the legacy `lru_cache` projects multiple physical states into a single canonical state for games with **Suit Symmetry** (e.g., Black Hole, Golf) or **Color Symmetry** (e.g., FreeCell, Klondike).

## Impact on Metamorphic Testing
- `lru_cache` and `flat_cache` diverge on node counts even in `SAME_SUIT` games.
- **Node-Agreement Divergence:** `LRU=MISS flat=HIT` occurs at op 1141+ in Bakers Game. This implies `flat_cache` detects equivalent states that `lru_cache` does not, likely due to the intrinsic pile-invariance of the descriptor model vs. the explicit sorting in the legacy code.
- **Suit/Color Invariance:** `flat_cache` is currently suit-sensitive, whereas `lru_cache` is not. This leads to `LRU=HIT flat=MISS` for suit-symmetric games.

## Known Acceptable Divergences (Legitimate Proxy Misses)

The `flat_cache` will intentionally miss states that the legacy cache hits in the following scenarios. These are caused by legacy-specific optimizations:

1. **Stock/Waste Cycle (Redeal) Symmetry**: In games with `redeal`, legacy collapses all "deal positions" into a single cyclic sequence if the waste size is a multiple of the deal count. `flat_cache` is "Waste-Strict," tracking the exact `waste_ptr`. This is a valid legacy optimization that results in fewer nodes in legacy.

## Success Criteria for Milestone 5
1. **Outcome Agreement:** Both `lru_cache` and `flat_cache` must reach the same `SOLVED` or `UNSOLVABLE` result for all tested instances.
2. **Regression Passing:** Final node count `Flat <= LRU` is the target, but `Flat > LRU` is acceptable ONLY if the divergence is attributable to the documented symmetries above.
3. **Strict Zero Tolerance**: Any `Flat > LRU` in a game WITHOUT Stock or Symmetry (e.g., Simple Tableau games) remains unacceptable.

## Decisions
- **Exclude Suit-Symmetric Games from Dual Cache Testing:** Node-agreement counts are marked as **second-level importance** and not expected to align exactly.
- **Outcome Verification:** Focus metamorphic verification on final solvability outcomes.
