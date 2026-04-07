# Handover Prompt: Evaluate Milestone 5 and Plan Milestone 6

## Context

The ReSolvitaire cache redesign is currently at the transition point between **Milestone 5 (Verification and Hardening)** and **Milestone 6 (Final Activation)**. 

The previous agent has implemented structural fixes in `game_state.cpp` and documented a "100% verification" status across 12 solitaire variants. However, **you must not take this completion for granted**. Your first responsibility is to critically evaluate these claims.

**Essential reference documents:**
1. `docs/cache-redesign/cache_agreement_validation.md` — The summary of verified games and the "ABC" classification of valid divergences.
2. `docs/cache-redesign/human_contributions.md` — Architectural insights and corrections that defined the verification guardrails.
3. `docs/cache-redesign/walkthrough.md` — Narrative of the verification process.
4. `CLAUDE.md` — Build and test instructions.

## Phase 1: Critical Evaluation of Milestone 5

Before proceeding to Milestone 6, you must independently verify that the `flat_cache` is indeed structurally sound.

1.  **Examine the Evidence**: Read `cache_agreement_validation.md` carefully. Do the explanations for Category A (Pile Symmetry), B (Waste Pointer), and C (Suit Symmetry) make sense based on the code?
2.  **Run the Tests**: Execute the `DualCacheTest` suite and regression Levels 1–3 in both Debug and Release modes. 
    - Verify that no unacceptable `LRU=HIT, Flat=MISS` events occur in non-Category B/C games.
    - Confirm that the `recompute_payload_from_scratch()` assertions in `game_state.cpp` pass during search.
3.  **Check for Residual Risk**: Inspect the recent changes in `game_state.cpp` (specifically the descriptor promotion logic). Ensure it handles all edge cases of card exposure.

## Phase 2: Milestone 6 Planning and Implementation

Once you have confirmed that the Milestone 5 status is acceptable, construct a detailed **Implementation Plan** for Milestone 6.

### Known Issues to Address in M6:
- **TwoBig1 Replacement Policy (flat_cache.cpp:L52-55)**: In the current `flat_cache::insert` implementation, when slot 1 is empty and slot 0 is occupied, the new state is simply placed into slot 1. This is incorrect. The policy should ensure that the state with the **lower depth** (the "Bigger" entry) always occupies slot 0. You should compare the new state's depth with the entry in slot 0 and swap them if the new state has a lower depth.

### Key Objectives for Milestone 6:
- **Final Cut-Over**: Switch the default solver cache from `lru_cache` to `flat_cache`.
- **Infrastructure Removal**: Deactivate the `dual_cache` wrapper and remove the diagnostic `recompute_payload_from_scratch()` overhead from production code paths.
- **Code Cleanup**: Remove any diagnostic `assert` or `cerr` blocks remaining in `game_state.cpp` and `solver.cpp`.
- **Performance Validation**: Use `scripts/compare_benchmarks.py` to produce a final report comparing the throughput (NPS) and memory usage of the new `flat_cache` against the legacy baseline.

## Reporting
At the end of your session, provide a clear summary of your evaluation of Milestone 5 and the result of the Milestone 6 activation.
