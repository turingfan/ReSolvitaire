# Prompt for Claude Code on the Web — KI-21 Bug Fixes

**Copy everything below the line into Claude Code on the Web as the initial prompt.**

---

## Context

PR #4 (`fix/ki21-waste-descriptor`) implements Known Issue #21 but has two correctness
bugs and a critical testing gap identified in code review. You need to fix these on
the existing branch and force-push.

## Setup

```bash
git checkout fix/ki21-waste-descriptor
git pull
```

## Before you start

Read these files:
1. `CLAUDE.md` — build commands and test gates
2. `docs/multiplicity-encoding/ki21-implementation-plan.md` — the plan
3. The PR review comment on PR #4 for full details of the bugs

Then read the code you will modify:
4. `src/main/game/search-state/game_state.cpp` lines 410-670 — the make_move/undo_move
   multiplicity blocks where the bugs are
5. `src/main/game/search-state/game_state.legal_moves.cpp` lines 259-342 —
   `generate_k_plus_moves_to_check()` and `add_stock_to_hole_foundation_moves()`
   to understand count=0 and hole targeting
6. `CMakeLists.txt` lines 377-475 — the trace_mult_vs_flat test definitions

## Rules

- **Bug in EXISTING code:** Log it in `docs/multiplicity-encoding/ki21-implementation-plan.md`
  Domain Questions section and continue.
- **Semantic/domain question:** Log it with a `// DOMAIN_QUESTION:` comment and continue.
- **All 3 test gates MUST pass** before pushing: `python3 scripts/run_tests.py`
- **Do NOT modify** files outside the scope of these fixes.

## Fix 1: count=0 overwrite bug (make_move)

In `game_state.cpp` `make_move`, the `stock_k_plus` case (~line 505):

The old-waste-top condition is:
```cpp
if (pre_move_waste_top_cid != UINT8_MAX
        && pre_move_waste_top_cid != post_waste_top_cid) {
```

This is missing a guard against overwriting the played card. When `count=0`,
`pre_move_waste_top_cid == played_cid`, so the played card's correct descriptor
(set in changes[0]) gets overwritten with `MLD_IN_STOCK`.

**Fix:** Add `&& pre_move_waste_top_cid != played_cid`:
```cpp
if (pre_move_waste_top_cid != UINT8_MAX
        && pre_move_waste_top_cid != played_cid
        && pre_move_waste_top_cid != post_waste_top_cid) {
```

## Fix 2: count=0 in undo_move

In `game_state.cpp` `undo_move`, the `stock_k_plus` case:

The played card is unconditionally set to `MLD_IN_STOCK`:
```cpp
mult_changes[mult_n++] = {pre_undo_played_cid,
    multiplicity_descriptor::make_locative(MLD_IN_STOCK)};
```

When count=0, after undo the played card returns to waste top — its descriptor
should be `mult_desc_at(waste, 0)`, not `MLD_IN_STOCK`. Currently this works by
accident because the post-undo waste top entry overwrites it, but this is fragile
and wrong for the cascade version of incremental_update.

**Fix:** Check whether the played card is the post-undo waste top before emitting:
```cpp
uint8_t post_undo_waste_top_cid = UINT8_MAX;
if (!piles[waste].empty()) {
    card nt = piles[waste].top_card();
    post_undo_waste_top_cid = zobrist_hash::card_id(
        nt.get_suit(), nt.get_rank());
}

// Played card: if it IS the post-undo waste top, use mult_desc_at;
// otherwise it returned to stock/waste interior -> MLD_IN_STOCK.
if (pre_undo_played_cid == post_undo_waste_top_cid) {
    mult_changes[mult_n++] = {pre_undo_played_cid,
        mult_desc_at(waste, 0)};
} else {
    mult_changes[mult_n++] = {pre_undo_played_cid,
        multiplicity_descriptor::make_locative(MLD_IN_STOCK)};
}

// Post-undo waste top (if different from played card)
if (post_undo_waste_top_cid != UINT8_MAX
        && post_undo_waste_top_cid != pre_undo_played_cid) {
    mult_changes[mult_n++] = {post_undo_waste_top_cid,
        mult_desc_at(waste, 0)};
}

// Pre-undo waste top (post-make top) no longer at pos 0
if (pre_undo_waste_top_cid != UINT8_MAX
        && pre_undo_waste_top_cid != post_undo_waste_top_cid
        && pre_undo_waste_top_cid != pre_undo_played_cid) {
    mult_changes[mult_n++] = {pre_undo_waste_top_cid,
        multiplicity_descriptor::make_locative(MLD_IN_STOCK)};
}
```

## Fix 3: Missing hole-top check

In both `make_move` and `undo_move` `stock_k_plus` cases, add hole-top handling.
Copy the pattern from the `regular` move case.

**make_move**, add after the waste-top logic:
```cpp
// If moved to hole, old hole top becomes PERMANENT
if (m.to == hole && piles[hole].size() > 1) {
    card old_top = piles[hole][1];
    uint8_t old_cid = zobrist_hash::card_id(
        old_top.get_suit(), old_top.get_rank());
    mult_changes[mult_n++] = {old_cid,
        multiplicity_descriptor::make_locative(MLD_PERMANENT)};
}
```

**undo_move**, add after the waste-top logic:
```cpp
// If m.to was hole, hole top changes back
if (m.to == hole && !piles[hole].empty()) {
    card new_top = piles[hole][0];
    uint8_t top_cid = zobrist_hash::card_id(
        new_top.get_suit(), new_top.get_rank());
    mult_changes[mult_n++] = {top_cid, mult_desc_at(hole, 0)};
}
```

## Fix 4: Restore trace tests for stock/waste games (CRITICAL)

**Revert the CMakeLists.txt changes** that converted `trace_mult_vs_flat_klondike`
and `trace_mult_vs_flat_canfield` to flat-vs-flat. Restore them to mult-vs-flat:

For `trace_mult_vs_flat_klondike` (~line 397), restore:
```
"--" "--type" "klondike-deal-1" "--random" "1"
     "--cache-type" "multiplicity" "--timeout" "30000"
```

For `trace_mult_vs_flat_canfield` (~line 432), restore:
```
"--" "--type" "canfield" "--random" "1"
     "--cache-type" "multiplicity" "--timeout" "30000"
```

Remove the added flat-vs-flat comments (the "KI-21 waste descriptor change" block
comment and the per-test comments).

**These tests MUST pass with the bug fixes applied.** The `--until-evict` flag means
they compare traces only up to the first eviction. If the multiplicity cache produces
the same hash as flat for every state, the HIT/MISS/INSERT sequence will be identical.
If these tests fail after the bug fixes, it means the multiplicity hash disagrees
with the flat hash for some state — investigate using the trace-debug skill (run both
caches, find the divergence point, dump both states).

**If the tests still fail after the bug fixes:** This is important information.
Use the trace-debug procedure:
```bash
# Build trace binaries
./build.sh --trace

# Run both caches
./cmake-build-trace/bin/solvitaire-flat-trace --type klondike-deal-1 --random 1 \
    --trace /tmp/trace-flat.trace
./cmake-build-trace/bin/solvitaire-trace --type klondike-deal-1 --random 1 \
    --cache-type multiplicity --trace /tmp/trace-mult.trace

# Find divergence
python3 scripts/compare_traces.py --full /tmp/trace-flat.trace /tmp/trace-mult.trace
```

If they diverge, report the divergence point, the event types (HIT vs MISS), and the
operation number. Do NOT convert the tests to flat-vs-flat. Instead, log the finding
in `docs/multiplicity-encoding/ki21-implementation-plan.md` and leave the tests
failing with a clear explanation in the commit message.

## Fix 5: Add count=0 unit test

Add a test to `src/test/unit_tests/multiplicity_incremental_test.cpp` that verifies
count=0 works correctly. This test should set up:
- waste: card 0 (top, MLD_IN_WASTE), card 1 (MLD_IN_STOCK)
- tableau: card 20 (MLD_IN_SPACE)

Simulate count=0 (play waste top directly):
- Card 0 (played) -> predecessor(20, false)
- Card 1 (new waste top) -> MLD_IN_WASTE

Verify with `verify_matches_scratch(eng)`.

## Execution order

1. Apply fixes 1-3 (code bugs)
2. Build and run: `python3 scripts/run_tests.py --quick` (unit tests only, fast check)
3. Apply fix 4 (restore trace tests)
4. Apply fix 5 (add count=0 test)
5. Full test run: `python3 scripts/run_tests.py`
6. If trace tests fail, follow the trace-debug procedure above
7. Commit and push

## Commit

```bash
git add -A
git commit -m "fix: KI-21 review fixes — count=0 bug, hole-top, restore trace tests

Bug fixes:
- count=0: add played_cid guard to prevent overwriting played card descriptor
- undo: explicit waste-top check instead of relying on overwrite order
- hole-top: add MLD_PERMANENT for old hole top in stock_k_plus (both directions)

Testing:
- Restore trace_mult_vs_flat_klondike and trace_mult_vs_flat_canfield as mult-vs-flat
- Add count=0 unit test
- All 3 test gates pass"

git push
```

## If you get stuck

If a trace test fails after the bug fixes, that is valuable diagnostic information.
Do NOT work around it by changing the test. Instead:
1. Run the trace-debug procedure to find the divergence
2. Log everything you find in the implementation plan
3. Commit what you have with a clear explanation
4. Push and note the failure in the PR
