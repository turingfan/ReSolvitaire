# Known Issues

## 1. JSON Deal Round-Trip Changes Node Counts (`json_helper.cpp`)

**Affected file:** `src/main/input-output/input/json-parsing/json_helper.cpp`

**Status:** Fixed in `claude/quizzical-darwin`; present in `testing-infrastructure` and
upstream Solvitaire `master`.

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

### Workaround (applied for levels 2–5)

The Level 2–5 regression runner invokes the solver with `--random <seed>` directly,
bypassing JSON serialisation entirely. The oracle values were generated from seed-based
runs in the original experimental dataset, making the comparison consistent.

Level 1 uses JSON instance files, but testing showed the bug does not affect those
instances at the streamliners used (all Level 1 outcomes and node counts match the
experimental ground truth exactly).

### Fix (in `claude/quizzical-darwin`)

In `json_helper::print_game_state_as_json`, line 90:

```cpp
// BUGGY — iterates the reordered list
for (auto pr : gs.tableau_piles) {

// CORRECT — iterates the original fixed order, matching what the parser expects
for (auto pr : gs.original_tableau_piles) {
```

This one-line change is applied in the `claude/quizzical-darwin` branch and should be
merged to `testing-infrastructure` via the pending PR.
