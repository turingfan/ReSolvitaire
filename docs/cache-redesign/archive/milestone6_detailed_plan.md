# Milestone 6: Remove Pile Ordering for Flat-Cache Games — Detailed Plan

**Status:** Ready to implement
**Prerequisite:** M5 declared done-enough; known open issues logged in `known-issues.md`
  and `bug_report_level2_regression_mismatches.md` and accepted as low risk for M6.
**Branch:** `refactor-caching`

---

## Goal

Eliminate the `eval_pile_order()` sorting in `place_card`/`take_card` for games using
the flat cache. The flat cache's descriptor-based encoding is inherently pile-order
independent (descriptors never reference pile indices), so the pile-order canonicalization
done for the LRU cache is unnecessary overhead. Removing it speeds up move execution and
simplifies the code path.

**Key insight (Ian Gent, contribution #9):** Card-centric encoding eliminates pile-symmetry
sorting. Each card's descriptor encodes *what* it's sitting on, not *which pile* it's in.

---

## Background: How Pile Ordering Works Now

### Data structures
- `tableau_piles` — `std::list<pile::ref>` — reordered at runtime so largest pile comes first
- `original_tableau_piles` — `std::vector<pile::ref>` — fixed deal order, never reordered
- Same pattern for `cells` and `reserve`

### Where ordering happens
- `game_state::place_card()` (~line 857 of `game_state.cpp`) — after placing a card, calls
  `eval_pile_order(pr, true)` to bubble the pile up in the sorted list
- `game_state::take_card()` (~line 869) — after taking a card, calls
  `eval_pile_order(pr, false)` to bubble the pile down
- Both are guarded by `#ifndef NO_PILE_SYMMETRY` and a runtime check for spider-deal games

### Where ordering is consumed
- **LRU cache** (`global_cache.cpp` line 75): iterates `gs.tableau_piles` (sorted list) to
  build `cached_game_state`. Sorted order means two boards differing only in which pile
  holds which cards hash identically — that is the canonicalization the LRU relies on.
- **Move generation** (`game_state.legal_moves.cpp`): iterates `tableau_piles` to enumerate
  moves. Sorted order gives a canonical move sequence.
- **Flat cache**: uses `original_tableau_piles` for init. Does NOT depend on sorted
  `tableau_piles` at all.

### What `eval_pile_order` does (`game_state.pile_order.cpp`)
Insertion-sort on the `std::list` after every card placement/removal. O(n) list traversal
per move, where n = number of tableau piles (up to 13 for SpanishPatience).

---

## Implementation Steps

### Step 1: Add `--force-lru` command-line flag

Add a runtime option that forces use of the LRU cache even for games that would normally
use the flat cache. This flag has two purposes:
1. Enables metamorphic testing (compare same instance under both caches)
2. Provides an escape hatch if a flat-cache bug is suspected in production

**In `command_line_helper.h`**, add a field:
```cpp
bool force_lru_cache = false;
```

**In `command_line_helper.cpp`**, add the option to the options description:
```cpp
("force-lru", "Force use of LRU cache even for flat-cache games")
```
And set the field when the option is present.

**In `solver.cpp`** (and `solvability_calc.cpp`), the cache construction currently reads:
```cpp
if (use_new_cache(rules)) { /* flat */ } else { /* lru */ }
```
Change to:
```cpp
if (use_new_cache(rules) && !options.force_lru_cache) { /* flat */ } else { /* lru */ }
```

The `options` struct (or however CLI args are passed to the solver) must carry this flag
through. Check how `streamliner_options` is currently threaded from CLI to solver — use
the same pattern.

### Step 2: Add `skip_pile_ordering` flag to `game_state`

In `game_state.h`, add a private member:
```cpp
bool skip_pile_ordering;
```

In the `game_state` constructors (there are multiple — the seed-based constructor and the
JSON-based constructor), set this flag after rules are known:
```cpp
skip_pile_ordering = use_new_cache(rules);
```

**Important:** This flag is set from `rules` alone, not from the CLI `--force-lru` flag.
The pile ordering decision is a property of the *game type*, not the cache choice.
When `--force-lru` is used, the LRU cache will receive an unsorted `tableau_piles` list —
but this is fine for testing purposes. (See Step 3 for more on this.)

You will need to `#include "cache_interface.h"` (or wherever `use_new_cache` is declared)
in `game_state.cpp`. Check if it is already included.

### Step 3: Guard `eval_pile_order` calls

In `game_state::place_card()` (~line 857):
```cpp
void game_state::place_card(pile::ref pr, card c) {
    piles[pr].place(c);

#ifndef NO_PILE_SYMMETRY
    if (!skip_pile_ordering
        && (rules.stock_size == 0 || rules.stock_deal_t != sdt::TABLEAU_PILES)) {
        eval_pile_order(pr, true);
    }
#endif
}
```

In `game_state::take_card()` (~line 869):
```cpp
card game_state::take_card(pile::ref pr) {
    card c = piles[pr].take();
#ifndef NO_PILE_SYMMETRY
    if (!skip_pile_ordering
        && (rules.stock_size == 0 || rules.stock_deal_t != sdt::TABLEAU_PILES)) {
        eval_pile_order(pr, false);
    }
#endif
    return c;
}
```

The existing spider-deal check is redundant when `skip_pile_ordering` is true (since
`use_new_cache` already excludes spider-deal games) but keep it for clarity and safety.

### Step 4: Verify `tableau_piles` iteration sites

When pile ordering is skipped, `tableau_piles` stays in its initial deal order.
Check that nothing breaks:


1. **Move generation** (`game_state.legal_moves.cpp`): iterates `tableau_piles`. Without
   pile ordering, moves are generated in deal order instead of size order. DFS exploration
   sequence changes, node counts change — correctness does not change.

2. **Dominance moves** (`game_state.dominance_moves.cpp`): iterates `tableau_piles` to
   find empty piles for auto-moves. A different first-empty-pile may be chosen; encoding
   is pile-independent so this doesn't affect correctness.

3. **JSON output** (`json_helper.cpp`): uses `gs.tableau_piles`. This is Known Issue #1
   in CLAUDE.md. Not made worse by M6.

4. **`init_payload_and_hash()`**: already uses `original_tableau_piles`. No change needed.

---

## Testing Approach: Metamorphic Comparison

Rather than maintaining static oracle files (which would all need updating after M6
changes node counts), testing is based on **running the same instance twice** — once
with the flat cache and once with `--force-lru` — and comparing the results.

This gives full control over what is compared, avoids creating new oracle files, and
naturally exercises exactly the games that matter (flat-cache games only).

### Agreement Levels

Not all games are expected to agree on node counts, due to known structural differences.
Two levels of agreement are defined:

**Full agreement** — outcome AND node counts must match exactly:
- Games with no known node-count issues under flat vs LRU

**Outcome-only agreement** — only solved/unsolvable/timeout must match:
- Games using streamliner "both" (suit symmetry active — node counts inherently differ)
- Hole games (inherent suit symmetry regardless of streamliner)
- Games with known M5 open issues (fortunes-favor, canfield-strict)

The set of games in each agreement level should be documented explicitly (see Testing
Checklist below). A future session can promote games from outcome-only to full agreement
as M5 bugs are resolved.

### How to Run

After building with `./build.sh --release`:

```bash
# Run with flat cache (default for qualifying games)
./cmake-build-release/solvitaire --type free-cell --random 1 --json > /tmp/flat.json

# Run with LRU cache forced
./cmake-build-release/solvitaire --type free-cell --random 1 --json --force-lru > /tmp/lru.json

# Compare outcomes and node counts
# (a small script or manual comparison — see below)
```

A simple comparison script (`scripts/compare_runs.py` or similar) can:
1. Parse the JSON output from both runs
2. Check outcome matches (always required)
3. Check node counts match (for full-agreement games)
4. Print a summary

The script does not need to be elaborate — even a shell one-liner comparing key fields
is sufficient for M6. The point is a repeatable, scriptable check with no static oracle.

### Games to Test

Focus on flat-cache games only. The following categories are known from Level 1/2
regression runs:

**Full agreement expected:**
- free-cell, bakers-game, somerset (no suit symmetry, no M5 known issues)
- seahaven-towers
- spanish-patience, flower-garden (these had false positives fixed in M5 — test to
  confirm they are stable)
- Other single-deck non-hole non-stock games

**Outcome-only agreement expected:**
- klondike (suit symmetry when streamliner "both")
- black-hole, golf, worm-hole (hole games — inherent suit symmetry)
- fortunes-favor, canfield-strict (M5 open bugs — outcomes correct, nodes differ)

**Do not test in M6:**
- Spider, gaps, accordion, two-deck games — these use LRU regardless of `--force-lru`
  and are unaffected by M6

### Running the Comparison

A suggested workflow for each game type:

```bash
GAME=free-cell
SEED=1
FLAT=$(./cmake-build-release/solvitaire --type $GAME --random $SEED --json)
LRU=$(./cmake-build-release/solvitaire --type $GAME --random $SEED --json --force-lru)

# Check outcome
FLAT_OUT=$(echo $FLAT | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['solution_type'])")
LRU_OUT=$(echo $LRU | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['solution_type'])")
[ "$FLAT_OUT" == "$LRU_OUT" ] && echo "OUTCOME OK" || echo "OUTCOME MISMATCH"

# Check node counts (full agreement only)
FLAT_N=$(echo $FLAT | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['states_searched'])")
LRU_N=$(echo $LRU | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['states_searched'])")
[ "$FLAT_N" == "$LRU_N" ] && echo "NODES OK" || echo "NODES DIFFER: flat=$FLAT_N lru=$LRU_N"
```

Note: node counts between flat and LRU will differ for ALL games after M6 (because pile
ordering is gone, DFS order is different). So "full agreement" means agreement after M6
between the two cache types at the NEW node counts — not agreement with pre-M6 counts.
Run at a range of seeds (e.g. 1–5) for each game type.

---

## What NOT to Do

- **Do NOT remove `eval_pile_order()` entirely** — still needed for LRU-cache games.
- **Do NOT remove the `tableau_piles` list** — move generation still iterates it.
- **Do NOT set `skip_pile_ordering` based on `--force-lru`** — pile ordering is a game
  property, not a cache property. LRU with unsorted piles is fine for testing.
- **Do NOT update regression oracle files** — node counts will change, but the new
  metamorphic testing approach makes static oracles unnecessary for M6 validation.
- **Do NOT run DualCacheTest as part of M6 validation** — it is not part of M6 testing.
  However, do NOT remove the dual cache infrastructure (`dual_cache.h/cpp`,
  `dual_cache_test.cpp`, `mismatch_diagnostic.cpp`). It remains a valuable debug tool
  for diagnosing flat cache correctness issues in later milestones.

---

## Files to Modify

| File | Change |
|---|---|
| `src/main/input-output/input/command_line_helper.h` | Add `bool force_lru_cache = false` |
| `src/main/input-output/input/command_line_helper.cpp` | Add `--force-lru` option |
| `src/main/solver/solver.cpp` | Check `force_lru_cache` in cache construction |
| `src/main/evaluation/solvability_calc.cpp` | Same |
| `src/main/game/search-state/game_state.h` | Add `bool skip_pile_ordering` member |
| `src/main/game/search-state/game_state.cpp` | Set flag in constructors; guard calls in `place_card`/`take_card` |

**Files NOT modified:**
- `game_state.pile_order.cpp` — code stays, just conditionally skipped
- `global_cache.cpp` / `global_cache.h` — unchanged
- `flat_cache.cpp` / `flat_cache.h` — unchanged
- `tests/oracles/` — not updated (metamorphic testing replaces oracle comparison)

---

## After M6: Extending Flat Cache Coverage

Once M6 is complete and the performance improvement is measured, the next phase is
extending flat cache coverage to game types currently excluded by `use_new_cache()`:
two-deck games, spider-type stock dealing (Spider solitaire), accordion, and sequence
games. This is only worth doing if M6 confirms the flat cache is meaningfully faster.

The `--force-lru` flag added in M6 will be useful here too: it allows side-by-side
comparison of flat vs LRU on newly-covered game types as each is added.

The dual cache infrastructure (`dual_cache`, `mismatch_diagnostic`) will be the primary
debugging tool for any correctness issues found when extending coverage.

---

## Expected Outcomes

1. **Solvability:** Identical between flat and LRU runs for all flat-cache games
2. **Node counts:** Will differ between flat and LRU runs (different DFS order + LRU has
   more false negatives without pile sorting). Within the same cache type, results are
   deterministic.
3. **Performance:** Measurable speedup for flat-cache games with many interchangeable
   piles. Games with few piles (e.g. klondike with 7) will show less improvement than
   games with many (e.g. spanish-patience with 13).
4. **LRU-cache games:** Completely unaffected — pile ordering still active for them.

---

## Testing Checklist

- [ ] Build succeeds: `./build.sh --release --unit-tests`
- [ ] Unit tests pass: `ctest -R unit_tests --output-on-failure`
- [ ] `--force-lru` flag works: `solvitaire --type free-cell --random 1 --force-lru` runs without error
- [ ] **Full-agreement games** (seeds 1–5): outcome AND node counts match between flat and `--force-lru` runs for: free-cell, bakers-game, somerset, seahaven-towers, spanish-patience, flower-garden
- [ ] **Outcome-only games**: outcomes match for klondike, black-hole, golf, fortunes-favor, canfield-strict
- [ ] LRU-only game (e.g. spider) produces identical results with and without `--force-lru` (flag has no effect)
- [ ] Measurable speedup on at least one multi-pile game (compare wall-clock before/after M6)

---

## Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Solvability outcome changes | Any outcome mismatch between flat and `--force-lru` = stop and investigate |
| `skip_pile_ordering` flag not set in all constructors | Search for ALL `game_state` constructors; set flag in each |
| `--force-lru` flag not threaded through to solver correctly | Trace the path from CLI parsing to solver construction; use same pattern as `streamliner_options` |
| Full-agreement games turn out to have node count differences | Investigate before reclassifying as outcome-only; may indicate a latent flat cache bug |
| JSON output order changes cosmetically | Known Issue #1 in CLAUDE.md; not a correctness problem |
