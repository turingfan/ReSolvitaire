# Known Issues

## 1. JSON Deal Round-Trip Changes Node Counts (`json_helper.cpp`)

**Affected file:** `src/main/input-output/input/json-parsing/json_helper.cpp`

**Present in:** Both ReSolvitaire (`testing-infrastructure` branch) and the original
upstream Solvitaire (`master` branch).

### Description

`json_helper::print_game_state_as_json` serialises the tableau piles by iterating
`gs.tableau_piles` — the *runtime-reordered* list maintained by `eval_pile_order` to
keep the largest pile first. However, `deal_parser::parse_tableau_piles` reads the piles
back into `gs.original_tableau_piles` — the *original fixed order* from construction.

When pile symmetry has reordered `tableau_piles` away from the original order, the
serialised JSON records piles in the reordered sequence, but parsing assigns those cards
back in the original positional order. The resulting game state has the same cards but
in a different internal arrangement, leading to:

- Different move-generation order
- Different transposition-table (cache) hit/miss patterns
- Different `states_searched` counts — even though the deal is logically identical

**Confirmed example:** `canfield-strict` seed 4000550 with `--streamliners both`
- Run from seed: **112 275** states searched
- Run from exported JSON: **112 266** states searched (9 fewer)

### Workaround (applied in this branch)

The Level 2–5 regression runner now invokes the solver with `--random <seed>` directly,
bypassing JSON serialisation entirely. The oracle values were generated from seed-based
runs in the original experimental dataset, so this makes the comparison consistent.

### Proper Fix

In `json_helper::print_game_state_as_json`, change:

```cpp
// BUGGY — iterates the reordered list
for (auto pr : gs.tableau_piles) {
```

to:

```cpp
// CORRECT — iterates the original fixed order, matching what the parser expects
for (auto pr : gs.original_tableau_piles) {
```

This has been fixed in `claude/quizzical-darwin` by applying the one-line change above.
Once merged, Level 1 oracles could be regenerated from seeds for full consistency.

---

## 2. Level 2 Oracle Had 10 Incorrect "Unsolvable" Entries (FIXED)

**Affected file:** `tests/oracles/level2.json`

**Detected:** 2026-03-19 (first visible after fixing the broken CTest invocation)

### Root Cause

All 10 instances are smart-run experiments where Run 1 (with streamliners) returned
"unsolvable" and Run 2 (without streamliners) returned "solved" — i.e., the ground
truth is "winnable". The bug in `export_test_deals.py` (the duplicate outcome block
at lines 119–126, now fixed) unconditionally overwrote `final_outcome` with Run 1's
result, discarding the correct Run 2 outcome.

### Fix

The `export_test_deals.py` bug was fixed in commit `10c233c`. Regenerating the Level 2
oracle with the fixed script corrects all 10 entries.
