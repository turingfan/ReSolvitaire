# Stage 5.3 — Solvability Cross-Check Results

**Date:** 2026-05-24
**Branch:** `multiplicity-encoding` (commit `05f45a8`)
**Method:** Compare `--cache-type multiplicity` (with incremental updates) vs `--cache-type auto` (LRU for suit-symmetry, flat otherwise)
**Timeout:** 120 seconds per instance

## Results: 110 comparisons, 0 mismatches — PASS

### Klondike COLOUR mode (suit-symmetry), seeds 1-20

| Seed | Multiplicity | Auto (LRU) | Match |
|------|-------------|------------|-------|
| 1 | unsolvable | unsolvable | yes |
| 2-14 | winnable | winnable | yes |
| 15 | unsolvable | unsolvable | yes |
| 16-18 | winnable | winnable | yes |
| 19 | unsolvable | unsolvable | yes |
| 20 | winnable | winnable | yes |

**20/20 match** (17 winnable, 3 unsolvable)

### Free-cell SI mode (suit-symmetry), seeds 1-20

All 20 seeds: winnable = winnable. **20/20 match.**

### Black-hole SI mode, seeds 1-20

| Seed | Multiplicity | Auto | Match |
|------|-------------|------|-------|
| 2 | unsolvable | unsolvable | yes |
| All others | winnable | winnable | yes |

**20/20 match** (19 winnable, 1 unsolvable)

### TABLEAU_PILES games, seeds 1-10

| Game | Results | Match |
|------|---------|-------|
| east-haven | seed 1 unsolvable, rest winnable | 10/10 |
| spiderette | all winnable | 10/10 |
| will-o-the-wisp | all winnable | 10/10 |

**30/30 match.**

### NONE mode regression: klondike seeds 1-20 (no streamliner)

| Seed | Multiplicity | Auto (Flat) | Match |
|------|-------------|------------|-------|
| 1 | unsolvable | unsolvable | yes |
| 2-14 | winnable | winnable | yes |
| 15 | unsolvable | unsolvable | yes |
| 16-18 | winnable | winnable | yes |
| 19 | unsolvable | unsolvable | yes |
| 20 | winnable | winnable | yes |

**20/20 match** (17 winnable, 3 unsolvable)

## Symmetry modes tested

| Mode | Game types | Seeds | Result |
|------|-----------|-------|--------|
| COLOUR (26 classes of 2) | klondike | 1-20 | 20/20 match |
| SUIT_IRRELEVANT (13 classes of 4) | free-cell, black-hole | 1-20 each | 40/40 match |
| NONE (52 classes of 1) | klondike (no streamliner), east-haven, spiderette, will-o-the-wisp | various | 50/50 match |

## Conclusion

The incremental multiplicity update engine (Stages 4+5) produces identical solvability
verdicts to the auto-dispatched cache (LRU for suit-symmetry games, flat for NONE mode)
across all tested game types, symmetry modes, and seeds. Zero mismatches.
