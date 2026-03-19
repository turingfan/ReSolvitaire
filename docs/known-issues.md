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

## 2. Level 2 Oracle Has 10 Incorrect "Unsolvable" Entries

**Affected file:** `tests/oracles/level2.json`

**Detected:** 2026-03-19 (first visible after fixing the broken CTest invocation)

### Description

Ten instances in the Level 2 oracle are marked `solution_type: "unsolvable"` (or
`"unwinnable"`) but the current solver resolves them as `"winnable"`. All ten are in the
`klondike-deal-N`, `free-cell-4-pile`, or `thirty` game families:

| Instance | Oracle | Solver |
|----------|--------|--------|
| `klondike-deal-1_700004_unwinnable.json` | unsolvable | winnable |
| `klondike-deal-1-noworryback_188733_unwinnable.json` | unsolvable | winnable |
| `klondike-deal-11_806981_unwinnable.json` | unsolvable | winnable |
| `klondike-deal-11-noworryback_903394_unwinnable.json` | unsolvable | winnable |
| `klondike-deal-12_695016_unwinnable.json` | unsolvable | winnable |
| `klondike-deal-12-noworryback_819657_unwinnable.json` | unsolvable | winnable |
| `klondike-deal-13_729723_unwinnable.json` | unsolvable | winnable |
| `klondike-deal-13-noworryback_72032_unwinnable.json` | unsolvable | winnable |
| `free-cell-4-pile_664642_unwinnable.json` | unsolvable | winnable |
| `thirty_7194_unwinnable.json` | unsolvable | winnable |

### Suspected Cause

The oracle data was curated from an older experimental dataset (Solvitaire v0.08.1).
These particular instances likely experienced a memout or a solver version difference that
caused incorrect "unsolvable" classifications. The memout filter in `export_test_deals.py`
uses `states_removed_from_cache > 0` to detect cache evictions, but the original dataset's
column mapping may have been mis-identified for these game types.

### Impact

These 10 instances cause Level 2 CTest to fail (150/160 pass). They do not indicate a
solver regression — the solver result is correct.

### Fix

Re-curate the Level 2 oracle by re-running the affected seeds through the current solver
and updating the oracle entries, or by excluding these instances if the original data
cannot be trusted for these game types.
