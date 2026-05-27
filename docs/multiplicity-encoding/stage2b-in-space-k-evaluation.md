# Stage 2B Evaluation — `in_space(k)` Pile-Indexed Locatives

Evaluate the implementation of pile-indexed locative descriptors for TABLEAU_PILES games.

---

## Checklist

### 1. Descriptor Assignment

- [x] `recompute_all()` uses `MLD_IN_SPACE + pile_idx` when
  `stock_deal_t == TABLEAU_PILES`
- [x] `recompute_all()` uses bare `MLD_IN_SPACE` (no pile index) when piles are symmetric
  (stock_size == 0 or stock_deal_t != TABLEAU_PILES)
- [x] `pile_idx` is the 0-based index within `original_tableau_piles`, not `tab_ref`
- [x] Verify: for a 7-pile game, locative kinds are 6,7,8,9,10,11,12
  (Zobrist columns 58..64, well within 80-column table)

### 2. Eligibility

- [x] `use_multiplicity_cache()` no longer excludes TABLEAU_PILES games
- [x] `use_new_cache()` still excludes TABLEAU_PILES (FlatPolicy can't handle them)
- [x] `--cache-type multiplicity` works for east-haven, spiderette, will-o-the-wisp

### 3. Asserts

- [x] Debug build of spiderette with `--cache-type multiplicity` does not trigger asserts
- [x] `game_state.cpp:895,915` asserts are compatible (they check `!use_new_cache(rules)`,
  which is true for TABLEAU_PILES regardless of multiplicity eligibility)

### 4. No regression

- [x] Klondike (pile-symmetric) with `--cache-type multiplicity`: identical states_searched
  to `--cache-type auto` on seeds 1-3 (0-eviction seeds 1 and 2; seed 3 has 2 vs 1
  evictions but identical states_searched — expected, different hash functions)
- [x] All 3 test gates pass

### 5. Correctness

- [x] Solvability matches between `--cache-type multiplicity` and `--cache-type auto` for
  seed 1 of east-haven (unsolvable), spiderette (winnable), will-o-the-wisp (winnable)
- [x] `MLD_COUNT` comment updated to clarify it counts base kinds only

### 6. Auto-dispatch

- [x] The premature auto-dispatch changes (suit_sym && use_multiplicity_cache) in main.cpp,
  benchmark.cpp, solvability_calc.cpp are reverted — multiplicity remains opt-in only

---

## Key Risk

The pile index must use the position within `original_tableau_piles` (0-based ordinal),
NOT the `pile::ref` value (which is an absolute pile index into the `piles` array and
includes foundations, cells, stock, waste, etc. before the tableau). Getting this wrong
would use huge locative kind values that overflow the Zobrist column range.

**Resolution:** The implementation correctly uses a separate `pile_idx` counter
incremented alongside the `for` loop over `ctx.original_tableau_piles`.

---

## Test Results (2026-05-18)

**TABLEAU_PILES solvability agreement (seed 1):**

| Game | multiplicity | auto (LRU) | Match |
|---|---|---|---|
| east-haven | unsolvable, 30414 states | unsolvable, 30414 states | ✓ |
| spiderette | winnable, 7044 states | winnable, 7044 states | ✓ |
| will-o-the-wisp | winnable, 555984 states | winnable, 555984 states | ✓ |

**Klondike pile-symmetric regression (states_searched):**

| Seed | multiplicity | auto (FlatPolicy) | Match |
|---|---|---|---|
| 1 | 158295 | 158295 | ✓ |
| 2 | 241 | 241 | ✓ |
| 3 | 927898 | 927898 | ✓ |

**Test gates:** all 3 passed (release unit tests, level-1 regression, debug unit tests).
