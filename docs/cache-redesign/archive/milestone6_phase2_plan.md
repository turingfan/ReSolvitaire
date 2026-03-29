# Milestone 6 Phase 2: Fix `--force-lru` Pile Ordering + Metamorphic Testing

**Status:** Ready to implement
**Branch:** `refactor-caching`
**Written:** 2026-03-28

---

## Context

M6 Phase 1 (committed in `f1112f2`) implemented:
- `skip_pile_ordering = use_new_cache(rules)` in `game_state`
- `--force-lru` CLI flag (forces LRU cache for flat-cache games)

**Problem discovered:** `--force-lru` also skips pile ordering (because
`skip_pile_ordering` is based on `use_new_cache(rules)` alone). This makes
`--force-lru` run a slow, non-commutative LRU that cannot be compared to the
oracle.

**Fix:** `skip_pile_ordering = use_new_cache(rules) && !force_lru`

This restores the old solver behaviour under `--force-lru`:
LRU with pile ordering, oracle-comparable, fast.

---

## Safety Verification

The expression `use_new_cache(rules) && !force_lru` is safe for all game types:

| Game type | `use_new_cache` | `force_lru` | `skip_pile_ordering` | Result |
|---|---|---|---|---|
| Spider (two_decks) | false | any | false | Pile ordering ON ✓ |
| Gaps/Accordion | false | any | false | Pile ordering ON ✓ |
| Free-cell (flat, default) | true | false | true | Ordering OFF (M6) ✓ |
| Free-cell (--force-lru) | true | true | false | Ordering ON (old) ✓ |

Additionally, `place_card`/`take_card` have a second guard for spider-type
stock dealing (`stock_deal_t == TABLEAU_PILES`) that independently prevents
`eval_pile_order`. Double-safe.

---

## Implementation Steps

### Step 1: Thread `force_lru` into `game_state` constructors

**`src/main/game/search-state/game_state.h`**

Change the private constructor declaration:
```cpp
explicit game_state(const sol_rules&, streamliner_options, bool force_lru = false);
```

Change the two public constructor declarations:
```cpp
explicit game_state(const sol_rules&, const rapidjson::Document&, streamliner_options, bool force_lru = false);
game_state(const sol_rules&, int seed, streamliner_options, bool force_lru = false);
```
(The initializer_list constructor does not need `force_lru` — test-only.)

**`src/main/game/search-state/game_state.cpp`**

Private constructor `game_state(const sol_rules& s_rules, streamliner_options stream_opts_, bool force_lru)`:
- Add `bool force_lru` parameter
- Change: `skip_pile_ordering = use_new_cache(s_rules) && !force_lru;`

Public constructors: add `bool force_lru = false` parameter and forward to
private constructor:
```cpp
game_state::game_state(const sol_rules& s_rules, const Document& doc, streamliner_options s_opts, bool force_lru)
        : game_state(s_rules, s_opts, force_lru) {
    deal_parser::parse(*this, doc);
    init_payload_and_hash();
}

game_state::game_state(const sol_rules& s_rules, int seed, streamliner_options s_opts, bool force_lru)
        : game_state(s_rules, s_opts, force_lru) {
    // ... seed-dealing code unchanged ...
    init_payload_and_hash();
}
```

**Important:** The initializer_list constructor does NOT need `force_lru`.
It always delegates to `game_state(s_rules, sos::NONE)` (no force_lru),
which is fine for tests.

### Step 2: Thread `force_lru` to `game_state` construction sites

**`src/main/main.cpp`** — inner `solve_game` function:
```cpp
game_state gs = seed ? game_state(rules, *seed, str_opts, force_lru)
                     : game_state(rules, *in_doc, str_opts, force_lru);
```

**`src/main/evaluation/solvability_calc.cpp`** — `solve_seed`:
```cpp
game_state gs(rules, seed, stream_opt, force_lru);
```

### Step 3: Revert the `two_decks = true` hack in GlobalCache tests

**`src/test/unit_tests/global_cache_test.cpp`**

Remove `rules.two_decks = true;` from `CommutativeTableauPiles`,
`CommutativeReserve`, and `CommutativeCells`.

These tests use minimal rules where `use_new_cache` returns true. With the
Phase 2 fix, the game_state constructors used in these tests (initializer_list
form) have `force_lru = false` (default). So `skip_pile_ordering = true`,
pile ordering is skipped.

**BUT** — these tests test LRU cache commutativity, which requires pile
ordering. The `two_decks` hack was the wrong fix.

**Correct fix:** Use the `force_lru = true` path in the test game_states so
that pile ordering is restored. Since the initializer_list constructor doesn't
accept `force_lru`, we need a different approach:

Option A (simplest): Keep `two_decks = true` — it correctly forces pile ordering
for LRU tests without needing force_lru in the constructor.

Option B: Use a preset game that is LRU-eligible (e.g. spider rules) so
`use_new_cache` naturally returns false.

**Recommended: Keep `two_decks = true` for now.** It is a valid fixture choice
(testing LRU commutativity requires an LRU-eligible game), not a hack. Remove
the explanatory comment only.

---

## Post-implementation Testing

### Step 4: Build and run unit tests

```bash
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R unit_tests$ --output-on-failure
```

Expected: all 182 tests pass, <25 seconds.

### Step 5: Oracle sanity check for `--force-lru`

Run `--force-lru` on a handful of Level 2 oracle seeds and verify exact node
count agreement with the oracle (confirming LRU path is identical to pre-M6).

Suggested: free-cell seeds 1–3, bakers-game seeds 1–3. Compare
`states_searched` against `tests/oracles/level2.json`.

A minimal shell one-liner per game:
```bash
cmake-build-release/bin/solvitaire --type free-cell --random 1 \
  --json --force-lru 2>/dev/null | python3 -c \
  "import sys,json; d=json.load(sys.stdin); print(d['states_searched'])"
```

Compare with oracle value. If they match → LRU path is clean.

Do NOT run this step without user approval.

### Step 6: Write metamorphic test script

**`scripts/metamorphic_test.sh`**

For each flat-cache game type, seeds 1–5:
1. Run flat cache (default)
2. Run `--force-lru`
3. Compare `solution_type` only (NOT node counts — they will differ because
   flat uses no pile ordering, LRU uses pile ordering)
4. Report PASS/FAIL per game+seed

Game types to test:
- **Outcome + node agreement with oracle (--force-lru only):** free-cell,
  bakers-game, somerset, seahaven-towers, spanish-patience, flower-garden
- **Outcome-only (flat vs --force-lru):** all of the above, plus klondike,
  black-hole, golf, fortunes-favor, canfield-strict

Do NOT run this script without user approval.

---

## Files Modified

| File | Change |
|---|---|
| `game_state.h` | Add `bool force_lru = false` to private + 2 public constructors |
| `game_state.cpp` | Forward `force_lru` through constructors; update `skip_pile_ordering` formula |
| `main.cpp` | Pass `force_lru` to `game_state` constructor |
| `solvability_calc.cpp` | Pass `force_lru` to `game_state` constructor |
| `global_cache_test.cpp` | Keep `two_decks = true` but clarify comment |
| `scripts/metamorphic_test.sh` | New: metamorphic testing script |

---

## Why NOT revert `two_decks = true` in GlobalCache tests

The initializer_list constructor has no `force_lru` parameter (by design —
test-only, not part of the solver path). These LRU commutativity tests need
pile ordering active. `two_decks = true` achieves this cleanly by making
`use_new_cache` return false, which is the correct way to express "this is
an LRU-eligible game". The comment should explain this rather than calling
it a "hack".
