# Stage 2B Evaluation — `in_space(k)` Pile-Indexed Locatives

Evaluate the implementation of pile-indexed locative descriptors for TABLEAU_PILES games.

---

## Checklist

### 1. Descriptor Assignment

- [ ] `recompute_all()` uses `MLD_IN_SPACE + pile_idx` when
  `stock_deal_t == TABLEAU_PILES`
- [ ] `recompute_all()` uses bare `MLD_IN_SPACE` (no pile index) when piles are symmetric
  (stock_size == 0 or stock_deal_t != TABLEAU_PILES)
- [ ] `pile_idx` is the 0-based index within `original_tableau_piles`, not `tab_ref`
- [ ] Verify: for a 7-pile game, locative kinds are 6,7,8,9,10,11,12
  (Zobrist columns 58..64, well within 80-column table)

### 2. Eligibility

- [ ] `use_multiplicity_cache()` no longer excludes TABLEAU_PILES games
- [ ] `use_new_cache()` still excludes TABLEAU_PILES (FlatPolicy can't handle them)
- [ ] `--cache-type multiplicity` works for east-haven, spiderette, will-o-the-wisp

### 3. Asserts

- [ ] Debug build of spiderette with `--cache-type multiplicity` does not trigger asserts
- [ ] `game_state.cpp:895,915` asserts are compatible (they check `!use_new_cache(rules)`,
  which is true for TABLEAU_PILES regardless of multiplicity eligibility)

### 4. No regression

- [ ] Klondike (pile-symmetric) with `--cache-type multiplicity`: identical states_searched
  to `--cache-type auto` on 0-eviction seeds
- [ ] All 3 test gates pass

### 5. Correctness

- [ ] Solvability matches between `--cache-type multiplicity` and `--cache-type auto` for
  at least 5 seeds each of east-haven, spiderette, will-o-the-wisp
- [ ] `MLD_COUNT` or equivalent constant updated if anything references it

### 6. Auto-dispatch

- [ ] The premature auto-dispatch changes (suit_sym && use_multiplicity_cache) in main.cpp,
  benchmark.cpp, solvability_calc.cpp are reverted — multiplicity remains opt-in only

---

## Key Risk

The pile index must use the position within `original_tableau_piles` (0-based ordinal),
NOT the `pile::ref` value (which is an absolute pile index into the `piles` array and
includes foundations, cells, stock, waste, etc. before the tableau). Getting this wrong
would use huge locative kind values that overflow the Zobrist column range.
