# KI-23 Fix: Centralise Suit-Symmetry Detection for Hole Games

**Branch:** `dev` (or a new feature branch from `dev`)
**Issue:** Known issue #23 — `docs/known-issues.md` lines 243-278
**Proposal:** `01-Knowledge-Base/Design-Documents/suit-symmetry-centralisation-proposal.md`

---

## Problem

Suit-symmetry eligibility is scattered across three layers with inconsistent logic.
The LRU cache auto-detects `rules.hole` and applies suit canonicalization. But the
dispatch layer computes `suit_sym` purely from `streamliner_options` — it never checks
`rules.hole`. So hole games (e.g. black-hole) miss suit-symmetry canonicalization on
non-LRU cache paths (flat, multiplicity, bitmap).

## What You Must Do

Implement **Option A** from the proposal: pass effective `suit_sym` separately, don't
modify `stream_opts`. Five steps:

### Step 1: Add `sol_rules::inherent_suit_symmetry()` method

**File:** `src/main/game/sol_rules.h`

Add a public method to `struct sol_rules`:

```cpp
/// True if this game's rules make all suits interchangeable regardless
/// of user-specified streamliners (e.g. hole-based games).
bool inherent_suit_symmetry() const { return hole; }
```

Place it near the other query methods (after the data members is fine). This is
the single centralised decision point.

### Step 2: Update all 4 dispatch `suit_sym` computations

There are exactly 4 places that compute `bool suit_sym`. Each must add
`|| rules.inherent_suit_symmetry()`:

1. **`src/main/main.cpp` line 128-129** — `dispatch_solve()`
2. **`src/main/evaluation/benchmark.cpp` line 149-150** — `dispatch_run_seed()`
3. **`src/main/evaluation/benchmark.cpp` line 194-195** — `dispatch_run_deal()`
4. **`src/main/evaluation/solvability_calc.cpp` line 186-187** — `solve_seed()`

Change each from:
```cpp
bool suit_sym = str_opts == game_state::streamliner_options::SUIT_SYMMETRY
             || str_opts == game_state::streamliner_options::BOTH;
```
To:
```cpp
bool suit_sym = str_opts == game_state::streamliner_options::SUIT_SYMMETRY
             || str_opts == game_state::streamliner_options::BOTH
             || rules.inherent_suit_symmetry();
```

(In `solvability_calc.cpp` the variable is `stream_opt` not `str_opts` — match the
existing name.)

### Step 3: Update `make_desc_ctx()` in game_state.cpp

**File:** `src/main/game/search-state/game_state.cpp` lines 1411-1419

Currently `make_desc_ctx()` sets `ctx.suit_sym` from `stream_opts` only. It must also
check `rules.hole`:

Change from:
```cpp
ctx.suit_sym = (stream_opts == streamliner_options::SUIT_SYMMETRY
                || stream_opts == streamliner_options::BOTH);
```
To:
```cpp
ctx.suit_sym = (stream_opts == streamliner_options::SUIT_SYMMETRY
                || stream_opts == streamliner_options::BOTH
                || rules.inherent_suit_symmetry());
```

This ensures that multiplicity descriptors and bitmap hashing use suit-canonical
Zobrist values for hole games.

### Step 4: Remove redundant `rules.hole` checks from LRU cache

Now that dispatch and `make_desc_ctx()` correctly set `suit_sym` for hole games,
the LRU cache's independent `|| gs.rules.hole` checks are redundant and must be
removed to avoid having the logic in two places.

**File:** `src/main/game/global_cache.h` lines 56-65

The hasher constructor currently has:
```cpp
, is_suit_symmetry(
      (gs.rules.foundations_present
          && (gs.stream_opts == GS::streamliner_options::SUIT_SYMMETRY
              || gs.stream_opts == GS::streamliner_options::BOTH))
      || gs.rules.hole) {}
```

**WAIT — there's a subtlety here.** The LRU hasher has a `foundations_present` guard
that the dispatch layer does NOT have. The dispatch `suit_sym` does not check
`foundations_present`. The `foundations_present` guard in the LRU hasher existed to
avoid applying suit-symmetry to games that have no foundations (where the suit
canonicalization would be incorrect). But `rules.hole` bypassed that guard — hole
games have no foundations but DO have inherent suit symmetry.

So the correct LRU hasher should be:
```cpp
, is_suit_symmetry(
      (gs.rules.foundations_present
          && (gs.stream_opts == GS::streamliner_options::SUIT_SYMMETRY
              || gs.stream_opts == GS::streamliner_options::BOTH))
      || gs.rules.inherent_suit_symmetry()) {}
```

This preserves the `foundations_present` guard for streamliner-based suit-symmetry
while using the centralised method for inherent suit symmetry. The effect is
identical to today's behaviour but the `rules.hole` knowledge is now in one place.

**File:** `src/main/game/global_cache.cpp` lines 114-117

Same pattern in `add_card()`:
```cpp
bool is_suit_symmetry = (gs.rules.foundations_present
        && (gs.stream_opts == GS::streamliner_options::SUIT_SYMMETRY
            || gs.stream_opts == GS::streamliner_options::BOTH))
        || gs.rules.hole;
```

Change `gs.rules.hole` to `gs.rules.inherent_suit_symmetry()`:
```cpp
bool is_suit_symmetry = (gs.rules.foundations_present
        && (gs.stream_opts == GS::streamliner_options::SUIT_SYMMETRY
            || gs.stream_opts == GS::streamliner_options::BOTH))
        || gs.rules.inherent_suit_symmetry();
```

### Step 5: Add a unit test

**File:** `src/test/unit_tests/` — create `suit_symmetry_dispatch_test.cpp` or add
to an existing file.

Write a test that verifies black-hole gets suit-symmetry canonicalization without
explicit `--streamliners`. The simplest approach:

```cpp
TEST(SuitSymmetryDispatch, BlackHoleHasInherentSuitSymmetry) {
    sol_rules rules = rules_parser::from_preset("black-hole");
    EXPECT_TRUE(rules.inherent_suit_symmetry());
    EXPECT_TRUE(rules.hole);
}

TEST(SuitSymmetryDispatch, FreeCellNoInherentSuitSymmetry) {
    sol_rules rules = rules_parser::from_preset("free-cell");
    EXPECT_FALSE(rules.inherent_suit_symmetry());
    EXPECT_FALSE(rules.hole);
}

TEST(SuitSymmetryDispatch, KlondikeNoInherentSuitSymmetry) {
    sol_rules rules = rules_parser::from_preset("klondike");
    EXPECT_FALSE(rules.inherent_suit_symmetry());
}
```

Also verify that the descriptor context picks up suit_sym for hole games:
```cpp
TEST(SuitSymmetryDispatch, BlackHoleDescriptorContextHasSuitSym) {
    zobrist_hash::init();
    sol_rules rules = rules_parser::from_preset("black-hole");
    // Use streamliner NONE — the point is that inherent symmetry kicks in
    game_state gs(rules, 1, game_state::streamliner_options::NONE);
    // make_desc_ctx is private, so test via the hash instead:
    // Two states that differ only by suit permutation should hash the same
    // This is hard to test directly — the rules-level test above is sufficient
}
```

The rules-level tests are the most important. If you can find a way to test the
hash-level behaviour (e.g. via a public method), great, but don't overcomplicate it.

## What NOT to Do

- Do NOT modify `streamliner_options` — the user's setting should be preserved
- Do NOT change `--streamliners` JSON output
- Do NOT add the `foundations_present` check to the dispatch-level `suit_sym` —
  that guard is specific to the LRU hasher's canonicalization logic
- Do NOT touch the multiplicity descriptor engine — it already handles
  `suit_sym = true` correctly via `SUIT_IRRELEVANT` mode

## Behavioral Changes (Expected)

- **Black-hole without explicit streamliners:** previously `suit_sym = false`, now
  `suit_sym = true`. On auto-dispatch this means it routes to multiplicity (which
  handles suit-sym) instead of flat (which doesn't). On LRU, no change (it already
  auto-detected).
- **All other games:** no change. `inherent_suit_symmetry()` returns false.

## Regression Expectations

- Level 1 oracle: black-hole instances may change node counts (fewer nodes due to
  suit-symmetry deduplication on multiplicity path). **Regenerate the oracle if
  node counts change.** Outcomes (solvable/unsolvable) must NOT change.
- Bitmap oracle: same — black-hole bitmap runs may change node counts.
- All non-hole games: zero change expected.

## Test Gates

All three gates must pass:
```bash
python3 scripts/run_tests.py
```

If oracle regeneration is needed:
```bash
cd cmake-build-release
ctest -R regression_level1 --output-on-failure
# If it fails due to node count changes on black-hole instances:
python3 scripts/regression_runner.py --regenerate --oracle tests/oracles/level1.json ...
```

## Files Changed (Summary)

| File | Change |
|---|---|
| `src/main/game/sol_rules.h` | Add `inherent_suit_symmetry()` |
| `src/main/main.cpp` | Add `\|\| rules.inherent_suit_symmetry()` to `suit_sym` |
| `src/main/evaluation/benchmark.cpp` | Same, two locations |
| `src/main/evaluation/solvability_calc.cpp` | Same |
| `src/main/game/search-state/game_state.cpp` | Add `\|\| rules.inherent_suit_symmetry()` to `make_desc_ctx()` |
| `src/main/game/global_cache.h` | Replace `gs.rules.hole` with `gs.rules.inherent_suit_symmetry()` |
| `src/main/game/global_cache.cpp` | Same |
| `src/test/unit_tests/` | New test file or additions |
