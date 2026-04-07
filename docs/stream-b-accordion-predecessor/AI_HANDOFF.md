# Stream B: Accordion Predecessor Cache — AI Handoff

**Branch:** `implement-accordion-predecessor`
**Base branch:** `dev`
**Date:** 2026-04-05
**Status:** Code structurally complete, 2 unit test failures requiring debugging

---

## Branch Context

This branch implements a predecessor-based transposition cache for accordion solitaire games. It is part of a two-stream parallel cache experiment:
- **Stream A** (`implement-hash-only-cache`): Hash-only cache, no payload — complete and passing
- **Stream B** (this branch): Predecessor encoding for accordion — has known undo bugs

Neither branch has been merged to `dev`. The user (Ian Gent) has explicitly requested no merges to dev until both are validated.

## Architecture Summary

### Cache Hierarchy

```
cache_interface (abstract)
├── lru_cache (Boost MultiIndex, legacy)
├── flat_cache (descriptor-based Zobrist, 32-byte entries)
├── hash_only_cache (Stream A: hash-only, 16-byte clusters)
├── predecessor_flat_cache (Stream B: predecessor encoding, 128-byte clusters)
└── dual_cache (metamorphic testing wrapper, takes two unique_ptr<cache_interface>)
```

### Key Files

| File | Role |
|---|---|
| `src/main/game/predecessor_state.h/cpp` | 64-byte state struct with zone markers enum |
| `src/main/game/predecessor_flat_cache.h/cpp` | Two-way clustered cache with hash guard |
| `src/main/game/search-state/game_state.h` | Added predecessor Zobrist fields, Z_pred table, undo structs |
| `src/main/game/search-state/game_state.cpp` | Predecessor init, update, undo in accordion moves |
| `src/main/game/cache_interface.h` | Added `use_predecessor_cache()` routing helper |
| `src/main/main.cpp` | 3-way cache dispatch (predecessor / flat / lru) |
| `src/main/solver/solver.cpp` | Sets predecessor depth, detects predecessor_flat_cache |
| `src/test/unit_tests/predecessor_cache_test.cpp` | 10 test cases |

### Predecessor Encoding Design

Each card stores an 8-bit value representing "what is beneath me":
- **0-51**: Card ID of the card below (suit*13 + rank-1)
- **52 (FINAL)**: Buried card that will never move again
- **53-56**: Zone markers (IN_CELL, IN_STOCK, IN_WASTE, IN_RESERVE)
- **57 (STARTING)**: Unmoved card
- **58+ (PILE_0+N)**: Bottom of accordion pile N (or tableau pile N)

For accordion: top cards form a left-to-right linked list via predecessors. The leftmost pile's top card has PILE_0. Each subsequent pile's top card has the previous pile's top card's ID as predecessor. All buried (non-top) cards have FINAL.

### Cache Structure (predecessor_flat_cache)

- 128-byte aligned clusters, each containing 2 cache lines
- Each cache line: 56-byte payload + 8-byte `other_hash` (Zobrist hash of the OTHER entry)
- Hash guard avoids second DRAM fetch: on lookup, slot 0 payload is always checked; slot 1 is only accessed if `lines[0].other_hash == probe_hash`
- TwoBig1 replacement: slot 0 depth-preferred, slot 1 always-replace

### Predecessor Zobrist Hash

- Separate `Z_pred[52][110]` table (static, initialised once with fixed seed `0xDEADBEEF42`)
- Incremental XOR: `hash ^= Z_pred[card_id][old_pred] ^ Z_pred[card_id][new_pred]`
- Independent from the existing descriptor-based Zobrist hash

### Accordion Move Predecessor Updates

When pile `from` merges onto pile `to` (from's cards placed on to, from removed from accordion list):

1. **to's top card** becomes buried: predecessor → FINAL
2. **from's top card** inherits to's old predecessor (takes to's chain position)
3. **Right-of-from's top card** (if exists and != to): predecessor → from's old predecessor
4. **Right-of-to's top card** (if exists and != from, and was pointing at to's top): predecessor → from's card_id

Each update is recorded in `pred_undo_entries` with a `pred_undo_frame` counting updates per move.

## Known Bugs — MUST FIX BEFORE PROCEEDING

### Bug 1: Predecessor payload not restored after undo

**Failing tests:** `PredecessorCacheTest.UndoRestoresToCachedState`, `PredecessorCacheTest.MultipleMovesAndUndos`

**Symptom:** After `make_move(m)` + `undo_move(m)`, `pred_payload` has all predecessor bytes = 0. The initial values before the move are correct non-zero values (valid zone markers and card IDs).

**Diagnostic data (from test output):**
- All 52 predecessor bytes are 0 after undo (every single card)
- The predecessor Zobrist hash DOES restore correctly (test 8 passes)
- This means `update_predecessor()` XOR logic works, but the payload rebuild is broken

**Likely root causes to investigate:**

1. **Payload rebuild in undo_accordion_move**: After restoring `predecessor_array` via undo stack, the code calls `pred_payload.clear()` (zeroes all 64 bytes) then copies from `predecessor_array`. If the array itself is all zeros at this point, the undo entries were not applied correctly. Add debug prints of `predecessor_array` values before and after the undo loop.

2. **Move type routing**: The test calls `gs.make_move(moves[0])` where `moves[0]` comes from `get_legal_moves()`. If the move is routed through `make_accordion_move`, predecessor tracking runs. But `get_dominance_move()` might intercept first and route differently. Check which move type the test actually processes by printing `moves[0].type`.

3. **Interaction with `make_built_group_move`**: `make_accordion_move` calls the standard `make_built_group_move` AFTER predecessor updates. In undo, `undo_built_group_move` runs BEFORE predecessor undo. If the standard undo modifies pile state in a way that affects the predecessor undo, that could cause issues. However, the predecessor undo only reads from the undo stack, not from piles, so this should be safe.

4. **pred_payload vs predecessor_array divergence**: The payload is rebuilt from `predecessor_array` in both make and undo. If the array is correct but payload is wrong, the copy loop itself might have a bug. Add an assertion comparing each `predecessor_array[i]` to `pred_payload.get_predecessor(i)` after the copy loop.

### Bug 2: Solver may hang on accordion games

The solver running `--type accordion --random 1` appeared to not terminate in initial testing. This is likely a consequence of Bug 1: without working undo, the cache never recognises revisited states, causing infinite loops in DFS.

## Test Status

| Test | Status |
|---|---|
| PredecessorCacheTest.AccordionUsesPredecessorCache | PASS |
| PredecessorCacheNonAccordion.FreeCellDoesNotUsePredecessorCache | PASS |
| PredecessorCacheTest.BasicInsertAndContains | PASS |
| PredecessorCacheTest.DuplicateInsertReturnsFalse | PASS |
| PredecessorCacheTest.DifferentStatesAreDistinct | PASS |
| PredecessorCacheTest.StateAfterMoveIsDifferent | PASS |
| **PredecessorCacheTest.UndoRestoresToCachedState** | **FAIL** |
| PredecessorCacheTest.PredecessorHashChangesAfterMove | PASS |
| PredecessorCacheTest.ClearEmptiesCache | PASS |
| **PredecessorCacheTest.MultipleMovesAndUndos** | **FAIL** |

All other unit tests (195 total) and accordion integration tests pass.

## What Needs Doing

1. **Fix the predecessor undo bug** (priority 1)
2. **Verify the solver runs correctly** on accordion games once undo works
3. **Run accordion integration tests** to confirm solver produces correct results
4. **Benchmark** predecessor_flat_cache vs lru_cache on accordion games
5. **Consider dual_cache testing**: Run predecessor_flat_cache against lru_cache via dual_cache to verify agreement

## Building and Testing

```bash
cd /path/to/ReSolvitaire-caching
cd cmake-build-release
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target unit_tests -j 8

# All unit tests
ctest -R unit_tests --output-on-failure

# Just predecessor tests
./bin/unit_tests --gtest_filter="PredecessorCache*"

# Accordion integration
ctest -R accordion --output-on-failure
```

## Suggested Prompt for Continuing AI Agent

```
You are working on the ReSolvitaire project, branch `implement-accordion-predecessor`.
This branch implements a predecessor-based transposition cache for accordion solitaire.

There are 2 failing unit tests caused by a bug in the predecessor undo logic.
Read docs/stream-b-accordion-predecessor/AI_HANDOFF.md for full context.

Your task:
1. Read the failing test code: src/test/unit_tests/predecessor_cache_test.cpp
   (tests UndoRestoresToCachedState and MultipleMovesAndUndos)
2. Read the accordion move functions: src/main/game/search-state/game_state.cpp
   (search for make_accordion_move and undo_accordion_move)
3. Read init_predecessor_state() in the same file
4. Debug why pred_payload has all-zero predecessors after undo
5. Fix the bug, rebuild, and verify all predecessor tests pass
6. Run: ctest -R accordion --output-on-failure to verify integration
7. Commit the fix

Key files:
- src/main/game/search-state/game_state.cpp (predecessor init + move/undo)
- src/main/game/search-state/game_state.h (predecessor fields)
- src/main/game/predecessor_state.h/cpp (64-byte state)
- src/main/game/predecessor_flat_cache.h/cpp (cache implementation)

The predecessor Zobrist hash restores correctly after undo (test 8 passes),
so the XOR logic in update_predecessor() is likely correct. The bug is in
how the payload is rebuilt from predecessor_array after the undo stack replay.

Do not merge to dev. Commit fixes to this branch only.
```
