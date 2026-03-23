# Prompt for Milestone 6: Final Activation of `flat_cache`

## Context

You are picking up the ReSolvitaire caching redesign after the successful completion of **Milestone 5 (Verification and Hardening)**. 

The `flat_cache` (Zobrist + `compact_state` descriptors) has been 100% verified for structural correctness against the legacy `lru_cache` using a metamorphic `dual_cache` wrapper and scratch-recomputation assertions. 

**Exhaustive documentation is available in:**
1. `docs/cache-redesign/cache_agreement_validation.md` — Definitive report on agreement levels and valid divergence categories (A/B/C).
2. `docs/cache-redesign/human_contributions.md` — Key architectural insights and corrections from the researcher (Ian Gent).
3. `docs/cache-redesign/walkthrough.md` — Summary of the verification process.
4. `CLAUDE.md` — Build and test instructions.

## Your Objective: Milestone 6

Your goal is to perform the final "cut-over" to `flat_cache` as the default implementation for all supported solitaire variants and remove the testing-only `dual_cache` infrastructure.

### 1. Evaluate Milestone 5 Results
- Review `cache_agreement_validation.md` to understand why specific games (Klondike, Spanish Patience, etc.) show node-count differences despite matching outcomes.
- Confirm all `unit_tests` and `regression_level1` through `regression_level3` pass.

### 2. Final Deployment Plan
1. **Switch Default Cache**: Update `solver.cpp` (and any related factory methods) to instantiate native `flat_cache` by default.
2. **Deactivate Dual-Cache**: Disable or remove the `dual_cache` wrapper and the `recompute_payload_from_scratch()` assertions from the default execution path.
3. **Clean Up Testing Code**: 
   - Remove diagnostic `assert` blocks related to Zobrist/Payload consistency checks in `game_state.cpp`.
   - Consider retaining `dual_cache_test.cpp` as an optional regression suite if it doesn't add significant build time.
4. **Final Benchmarking**:
   - Use `scripts/compare_benchmarks.py` to compare the final `flat_cache` performance against the legacy `lru_cache` baseline across several game types.
   - Verify that the memory-efficient `flat_cache` provides the expected throughput (Nodes Per Second) benefits.

### 3. Safety Check: Valid Mismatches
Remember that `flat_cache` is **not expected** to match legacy node counts in:
- **Category A**: Pile-Sorting (Flat finds *more* hits due to inherent card-centric symmetry).
- **Category B**: Waste-Pointer (Flat finds *fewer* hits because legacy uses a risky circular waste optimization).
- **Category C**: Suit Symmetry (Flat finds *fewer* hits in games with suit-interchangeability like Spanish Patience).

Any other divergence is a regression.

## Deliverables
- A production-ready codebase with `flat_cache` enabled.
- A final performance report (using the automated benchmarking suite).
- Updated documentation confirming the successful activation.
