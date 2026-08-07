# Bitmap Cache Stage 2 — Detailed Specification

This is the detailed reference for Stage 2. The short copyable prompt is in
`stage2-short-prompt.md`.

## Task

Integrate the `bitmap_cache` into the solver via a new `BitmapPolicy`. After this
stage, `--cache-type bitmap` should work for all single-deck, non-accordion games.

## Setup

```bash
git fetch origin
git checkout feature/bitmap-cache && git pull origin feature/bitmap-cache
git checkout -b feature/bitmap-stage2
```

## Before You Write Code

Read these files in order. Do not start coding until you have read all of them.

1. `CLAUDE.md` — build commands, test gates, project rules
2. `docs/bitmap-cache/domain-questions.md` — where to log domain issues
3. `src/main/game/bitmap_cache.h` — the Stage 1 bitmap cache class you'll integrate
4. `src/main/game/cache_policy.h` — existing policy structs. Copy this pattern exactly.
5. `src/main/game/cache_interface.h` — `use_new_cache()`, `use_multiplicity_cache()`,
   `use_predecessor_cache()` eligibility functions
6. `src/main/main.cpp` lines 118-160 — `dispatch_solve()` function
7. `src/main/evaluation/benchmark.cpp` lines 144-212 — `dispatch_run_seed()` and
   `dispatch_run_deal()` — these mirror `dispatch_solve()` and need the same changes
8. `src/main/evaluation/solvability_calc.cpp` lines 180-212 — `solve_seed()` dispatch
9. `src/main/input-output/input/command_line_helper.cpp` lines 91 and 261-263 —
   cache-type parsing and validation
10. `src/main/game/search-state/game_state.h` lines 60-110 — `game_state_impl<Policy>`
    template, `get_zobrist_hash()`, policy traits
11. `src/main/game/flat_descriptor_engine.h` — the descriptor engine used by FlatPolicy

## Implementation Steps

### Step 1: Add BitmapPolicy to cache_policy.h

Add after the existing policies:

```cpp
struct BitmapPolicy {
    static constexpr bool computes_hash = true;
    static constexpr bool computes_descriptor = true;
    static constexpr bool computes_multiplicity_descriptor = false;
    static constexpr bool skip_pile_ordering = true;
    using cache_type = bitmap_cache;
    using descriptor_engine = flat_descriptor_engine;
    using descriptor_store_type = compact_state;
};
```

**Why `computes_descriptor = true`:** The Zobrist hash is maintained incrementally via
descriptor updates in `game_state.cpp`. You cannot compute the hash without computing
descriptors. The bitmap cache discards the payload — but the descriptor computation
must still happen to keep the hash correct.

Add the necessary `#include "bitmap_cache.h"` at the top of `cache_policy.h`.

### Step 2: Add use_bitmap_cache() to cache_interface.h

Add an eligibility function. Bitmap uses the same hash as flat, so it has the same
eligibility. But it also works with suit-symmetry (the hash may be less useful for
suit-sym, but it won't crash). For now, use the same eligibility as `use_new_cache()`
but without the suit-symmetry restriction:

```cpp
inline bool use_bitmap_cache(const sol_rules& rules) {
    return !rules.two_decks
        && rules.sequence_count == 0
        && rules.accordion_size == 0;
}
```

Note: this is deliberately broader than `use_new_cache()` — no stock_deal_type check,
no suit_symmetry check. The bitmap cache doesn't need pile ordering or suit
canonicalisation because it doesn't store or verify payloads.

### Step 3: Add "bitmap" to command_line_helper.cpp

In the cache-type validation (around line 262), add `"bitmap"` as a valid value:

```cpp
if (cache_type != "auto" && cache_type != "hash-only"
    && cache_type != "multiplicity" && cache_type != "bitmap") {
```

### Step 4: Add dispatch in main.cpp

In `dispatch_solve()`, in the `#else` block (the default binary path), add bitmap
dispatch. Insert it after `force_lru` and before the existing explicit cache-type
checks:

```cpp
} else if (cache_type == "bitmap" && use_bitmap_cache(rules)) {
    return solve_game_impl<BitmapPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc);
```

### Step 5: Mirror dispatch in benchmark.cpp and solvability_calc.cpp

Add the same `cache_type == "bitmap"` check in:
- `benchmark.cpp:dispatch_run_seed()` — same pattern as main.cpp
- `benchmark.cpp:dispatch_run_deal()` — same pattern (no force_lru here)
- `solvability_calc.cpp:solve_seed()` — same pattern

### Step 6: Verify compilation and basic functionality

Build and verify these smoke tests work:

```bash
./build.sh --release --unit-tests
./cmake-build-release/bin/solvitaire --type klondike --random 1 --cache-type bitmap --json
./cmake-build-release/bin/solvitaire --type free-cell --random 1 --cache-type bitmap --json
./cmake-build-release/bin/solvitaire --type black-hole --random 1 --cache-type bitmap --json
```

All three should produce valid JSON with a `solution_type`. Compare with `--cache-type flat`
on the same seeds — solvable results must agree. Unsolvable results may differ (bitmap
false positives are expected).

### Step 7: Run all 3 test gates

```bash
python3 scripts/run_tests.py
```

All must pass. `SearchTraceAgreementTest.HashOnlyVsFlat_Klondike50Seeds` is a known
pre-existing skip — ignore it if it appears as excluded.

## Files to Create

None.

## Files to Modify

| File | Change |
|---|---|
| `src/main/game/cache_policy.h` | Add `BitmapPolicy` struct + include |
| `src/main/game/cache_interface.h` | Add `use_bitmap_cache()` function |
| `src/main/main.cpp` | Add bitmap dispatch in `dispatch_solve()` |
| `src/main/evaluation/benchmark.cpp` | Add bitmap dispatch in `dispatch_run_seed()` and `dispatch_run_deal()` |
| `src/main/evaluation/solvability_calc.cpp` | Add bitmap dispatch in `solve_seed()` |
| `src/main/input-output/input/command_line_helper.cpp` | Add `"bitmap"` to valid cache types |

Do NOT modify any other files.

## Adapted Rules

**Bug in EXISTING code:** Do NOT stop. Log the symptom in
`docs/bitmap-cache/domain-questions.md`. Continue with best-effort. Mark with
`// DOMAIN_QUESTION:` comment.

**Semantic/domain question you can't resolve from code or docs:** Log it in the same
place. Make your best guess and mark with `// DOMAIN_QUESTION:`.

**Bug in YOUR new code:** Fix normally — up to 3 attempts. If still failing, log and
submit what you have.

**Test gates:** All 3 gates MUST pass. Run `python3 scripts/run_tests.py`. If a gate
fails after 3 fix attempts, create the PR anyway and note the failure.

**Scope:** Only modify the files listed above. Do NOT refactor existing code.

**Commits:** One commit per logical step (e.g. "add BitmapPolicy", "add dispatch",
"add CLI option"). Not one giant commit.

## PR Creation

```bash
git push -u origin feature/bitmap-stage2
gh pr create --base feature/bitmap-cache --title "Stage 2: BitmapPolicy and solver integration" --body "$(cat <<'EOF'
## Summary

- BitmapPolicy added to cache_policy.h (uses flat descriptor engine, bitmap_cache)
- `--cache-type bitmap` CLI option
- Dispatch in main.cpp, benchmark.cpp, solvability_calc.cpp
- Eligibility: single-deck, non-accordion (broader than flat — no suit-sym restriction)

## Smoke Tests

- [ ] `--type klondike --random 1 --cache-type bitmap --json` produces valid result
- [ ] `--type free-cell --random 1 --cache-type bitmap --json` produces valid result
- [ ] `--type black-hole --random 1 --cache-type bitmap --json` produces valid result
- [ ] Solvable results agree with `--cache-type flat` on same seeds

## Test Results

- [ ] Gate 1 (Release): pass/fail
- [ ] Gate 2 (Trace): pass/fail
- [ ] Gate 3 (Debug): pass/fail

## Domain Questions

(Any entries added to `docs/bitmap-cache/domain-questions.md`)

---
_Generated by Claude Code_
EOF
)"
```
