# Prompt for Claude Code on the Web — KI-21 Waste Descriptor Fix

**Copy everything below the line into Claude Code on the Web as the initial prompt.**

---

## Task

Implement Known Issue #21 (waste descriptor O(stock) fix) for the ReSolvitaire multiplicity encoding. The implementation plan is at `docs/multiplicity-encoding/ki21-implementation-plan.md` — read it first.

## Setup

```bash
git checkout multiplicity-encoding
git checkout -b fix/ki21-waste-descriptor
```

## Before You Write Code

1. Read `CLAUDE.md` in the repo root (build commands, test gates, architecture)
2. Read `docs/multiplicity-encoding/ki21-implementation-plan.md` (the full plan)
3. Read `docs/known-issues.md` issue #21 (the problem statement)
4. Read these source files to understand the existing code:
   - `src/main/game/multiplicity_descriptor.h` (descriptor types)
   - `src/main/game/multiplicity_descriptor_engine.h` lines 340-420 (`recompute_all()` stock/waste section)
   - `src/main/game/search-state/game_state.cpp` lines 410-620 (`make_move`/`undo_move` multiplicity blocks)
   - `src/main/game/search-state/game_state.cpp` lines 1342-1400 (`mult_desc_at()`)
   - `src/main/game/search-state/game_state.cpp` lines 916-1050 (`make_stock_k_plus_move`/`undo_stock_k_plus_move`)
5. Confirm to yourself what you've read and what you plan to do

## Adapted Rules (for autonomous operation)

These rules override the standard AGENTS.md rules for this task:

- **Bug in EXISTING code:** Do NOT stop. Log the symptom clearly in the "Domain Questions Log" section at the bottom of `docs/multiplicity-encoding/ki21-implementation-plan.md`. Continue with your best-effort implementation.
- **Semantic/domain question you can't resolve from code or docs:** Log it in the same place. Make your best guess and mark it with a `// DOMAIN_QUESTION:` comment in the code.
- **Bug in YOUR new code (test failures, compilation errors):** Fix normally — that's development.
- **Test gates:** All 3 test gates MUST pass before creating the PR. Run: `python3 scripts/run_tests.py`
- **Do NOT modify** files outside the scope listed in the implementation plan
- **Do NOT add** new enum values to `multiplicity_locative` — reuse `MLD_IN_WASTE` for top-of-waste

## Implementation Steps

Follow the implementation plan. The order is:

### Step 1: Part 1 — Descriptor semantics change (the easy part)

1. Change `recompute_all()` in `multiplicity_descriptor_engine.h` (~line 371-378): only waste top gets `MLD_IN_WASTE`, rest get `MLD_IN_STOCK`
2. Change `mult_desc_at()` in `game_state.cpp` (~line 1368-1373): only `pos == 0` returns `MLD_IN_WASTE` for waste cards
3. Build and run tests: `python3 scripts/run_tests.py`
4. Part 1 alone should pass all tests because `stock_k_plus` still uses `recompute_all()` fallback, and the debug assertion `verify_against_scratch()` checks that `mult_desc_at()` agrees with `recompute_all()`.

### Step 2: Part 2 — Incremental updates for stock_k_plus

1. Add pre-move state captures before the `make_move` switch and `undo_move` switch (see plan for exact code)
2. Replace `mult_fallback = true` for `stock_k_plus` with incremental change computation in both `make_move` and `undo_move` (see plan for detailed logic)
3. Keep `stock_to_all_tableau` as `mult_fallback = true`
4. Build and run tests: `python3 scripts/run_tests.py`
5. The debug build is the critical gate — `verify_against_scratch()` runs on every move and catches any incremental/from-scratch disagreement

### Step 3: Unit tests

Add tests to `src/test/unit_tests/multiplicity_incremental_test.cpp` as described in the implementation plan.

### Step 4: Solvability cross-check

Run the solvability verification commands from the implementation plan to confirm multiplicity and auto cache agree on klondike outcomes.

### Step 5: Create PR

```bash
git add -A
git commit -m "fix: KI-21 waste descriptor O(stock) → O(1) for stock_k_plus moves

Collapse MLD_IN_WASTE to top-of-waste only. Non-top waste cards use MLD_IN_STOCK,
matching stock cards. This makes stock_k_plus moves O(1) descriptor changes instead
of O(stock_size), enabling incremental multiplicity updates.

Changes:
- recompute_all(): only waste[0] gets MLD_IN_WASTE
- mult_desc_at(): pos==0 check for waste pile
- make_move/undo_move: stock_k_plus incremental instead of fallback
- Unit tests for stock_k_plus incremental correctness"

git push -u origin fix/ki21-waste-descriptor
```

Then create a PR targeting `multiplicity-encoding` (NOT `dev`):
```bash
gh pr create --base multiplicity-encoding --title "fix: KI-21 waste descriptor O(1) for stock_k_plus" --body "## Summary
- Collapse MLD_IN_WASTE to top-of-waste only (non-top waste cards use MLD_IN_STOCK)
- Replace mult_fallback for stock_k_plus with O(1) incremental updates
- All 3 test gates pass

## Implementation
Following plan at docs/multiplicity-encoding/ki21-implementation-plan.md

## Domain questions / concerns
(List any issues logged during implementation)

## Test plan
- [ ] All 3 test gates pass (python3 scripts/run_tests.py)
- [ ] Debug build exercises verify_against_scratch() on every move
- [ ] Solvability cross-check: klondike seeds 1-20 agree between multiplicity and auto
- [ ] New unit tests for stock_k_plus incremental"
```

## If You Get Stuck

If you hit a problem you can't resolve after 2-3 attempts:
1. Log it in the implementation plan's Domain Questions section
2. If Part 2 (incremental) is too hard, submit Part 1 only (descriptor change + fallback preserved) — that's still valuable
3. Create the PR with whatever you have and note what's incomplete
