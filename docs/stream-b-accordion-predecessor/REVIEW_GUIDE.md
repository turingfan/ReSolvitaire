# Stream B: Accordion Predecessor Cache — Review Guide

**Branch:** `implement-accordion-predecessor` (based on `dev`)
**Date:** 2026-04-05
**Status:** Code structurally complete, 2 known test failures (predecessor undo bug)

---

## What Was Implemented

A predecessor-based transposition cache for accordion games. Instead of the descriptor-based encoding used by `flat_cache` (which doesn't work for accordion because piles merge and disappear), each card stores an 8-bit "predecessor" value — the card it sits on, or a zone marker indicating its position type.

### New Files

| File | Purpose |
|---|---|
| `predecessor_state.h/cpp` | 64-byte fixed-size state: byte 0 occupied, byte 1 depth, bytes 2-53 predecessor array |
| `predecessor_flat_cache.h/cpp` | 128-byte aligned two-way clusters with hash guard, TwoBig1 replacement |
| `predecessor_cache_test.cpp` | 10 GoogleTest cases |

### Key Design Decisions

1. **Predecessor encoding**: Each card stores one of: card_id (0-51) of the card beneath it, or a zone marker (FINAL=52, IN_CELL=53, ..., PILE_0=58+N). For accordion, top cards form a linked list via predecessors; buried cards get FINAL.

2. **128-byte clusters with hash guard**: Two 64-byte cache lines per cluster. Each line holds a 56-byte payload + 8-byte hash of the OTHER entry. On lookup, slot 0 is checked first (same cache line, already fetched). Slot 1 is only accessed if its hash guard matches — avoiding a second DRAM fetch for ~100% of non-matching probes.

3. **TwoBig1 replacement**: Slot 0 is depth-preferred, slot 1 always-replace. Same policy as the existing `flat_cache`.

4. **Predecessor Zobrist hash**: Independent `Z_pred[52][110]` table, XOR-updated incrementally on each predecessor change. Separate from the existing descriptor-based Zobrist hash.

5. **Undo stack with frames**: Each accordion move records 2-4 predecessor updates in `pred_undo_entries`, with a `pred_undo_frame` counting how many entries belong to that move. Undo replays in reverse.

## Changes Not Obviously Related to Caching

1. **`cache_interface.h`**: Added `use_predecessor_cache()` helper function (mirrors existing `use_new_cache()`).

2. **`CMakeLists.txt`**: Added `-Wno-nontrivial-memcall` to suppress a pre-existing RapidJSON warning that became an error with the current toolchain. This fix was also applied to `dev`.

3. **Cache routing in `main.cpp`, `solvability_calc.cpp`, `benchmark.cpp`**: Each now checks `use_predecessor_cache()` before `use_new_cache()`, creating a 3-way dispatch: predecessor_flat_cache / flat_cache / lru_cache.

4. **`solver.cpp`**: Predecessor depth is set alongside regular payload depth before cache insert. The `using_flat_cache` flag now also detects `predecessor_flat_cache` (to skip LRU-specific code paths).

## Known Bugs

**Two unit tests fail: `UndoRestoresToCachedState` and `MultipleMovesAndUndos`.**

Diagnostic findings:
- After make_move + undo_move, the `pred_payload` has all predecessor bytes = 0
- The initial predecessor values (before the move) are correct non-zero values
- The `predecessor_array[]` restoration via the undo stack may be correct, but the payload rebuild (`pred_payload.clear()` + loop copy) appears to produce all zeros
- The hash restoration (test 8) PASSES, meaning `update_predecessor()` XOR logic is likely correct
- Root cause is not yet identified — could be in the payload rebuild, in the undo entry recording, or in an interaction with the standard `undo_built_group_move` that runs before the predecessor undo

**Impact:** The cache will fail to recognise previously-seen states after backtracking, causing the solver to revisit states. The solver will still find correct solutions but will be slower (potentially much slower) than with a working cache.

## What to Look For in Code Review

1. **`make_accordion_move` predecessor logic** (game_state.cpp ~883-949): The 3-4 updates per merge are the most subtle part. Verify the linked-list reasoning: which cards need their predecessor updated when `from` merges into `to` and `from` is removed.

2. **Undo ordering**: Predecessor updates happen BEFORE pile operations in `make_accordion_move`, but undo happens AFTER pile restoration in `undo_accordion_move`. Check this ordering is correct — the pile state must be restored before the predecessor undo reads pile contents (but it doesn't read pile contents, it just replays the undo stack, so ordering may not matter).

3. **`init_predecessor_state()`** (game_state.cpp ~1437): Sets all 52 cards to STARTING, then walks the accordion list left-to-right setting predecessors. Accordion deals 52 cards to 52 piles (one per pile). Verify the chain linkage is correct.

4. **Payload rebuild**: Both make and undo call `pred_payload.clear()` then loop-copy from `predecessor_array`. This is the likely location of the bug. Check whether `clear()` is clobbering something that should be preserved.

5. **`predecessor_flat_cache` hash guard logic**: The `other_hash` field stores the hash of the OTHER entry's state, used to avoid fetching cache line 1. Verify the insert and contains paths are consistent about which hash goes where.

## Testing

```bash
# Build
cd cmake-build-release
cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . --target unit_tests -j 8

# Run all unit tests (195 pass, 2 fail — the predecessor undo tests)
ctest -R unit_tests --output-on-failure

# Run just predecessor tests
./bin/unit_tests --gtest_filter="PredecessorCache*"

# Run accordion integration test (uses the predecessor cache via normal routing)
ctest -R accordion --output-on-failure
```

## Benchmarking (after bugs fixed)

```bash
# Compare predecessor_flat_cache vs lru_cache on accordion
./bin/solvitaire --type accordion --random 1 --json
./bin/solvitaire --type accordion --random 1 --json --force-lru

# Solvability comparison
./bin/solvitaire --type accordion --solvability 100 --timeout 60000 --json
./bin/solvitaire --type accordion --solvability 100 --timeout 60000 --json --force-lru
```
