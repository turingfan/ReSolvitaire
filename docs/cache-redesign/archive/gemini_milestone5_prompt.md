# Prompt for Gemini: Implement Milestone 5 — Verification and Hardening

## Context

You previously implemented Milestone 4 (wiring flat_cache into the solver). Now you need to **verify correctness** across all game types using the regression suite and metamorphic testing.

**Read these files before starting:**
1. `docs/cache-redesign/milestone5_detailed_plan.md` — **Your primary reference. Follow it precisely.**
2. `docs/cache-redesign/metamorphic_cache_testing.md` — Background on the dual-cache testing approach
3. `CLAUDE.md` — Build commands, testing instructions

## Summary of What You're Doing

1. **Run regression Levels 1–3** — these cover ~80 game types with known oracle outcomes. Both flat_cache and lru_cache games are exercised. All outcomes must match.

2. **Implement `dual_cache`** — a wrapper that runs both lru_cache and flat_cache in parallel, asserting they agree on every insert/contains call (pre-eviction). This is the metamorphic testing framework.

3. **Add debug payload recomputation assertion** — in debug builds, recompute the compact_state payload from scratch and assert it matches the incremental payload. This catches any make_move/undo_move update bugs.

4. **Write dual_cache unit tests** — test multiple game types (BlackHole, FreeCell, Klondike, SpanishPatience) with large caches (no eviction → full agreement) and small caches (outcome agreement).

5. **Edge case tests** — very small cache, games with pre-filled cells (eight-off), games with stock redeal (canfield).

## Files to Create

| File | Purpose |
|---|---|
| `src/main/game/dual_cache.h` | Header-only dual-cache wrapper |
| `src/test/unit_tests/dual_cache_test.cpp` | Metamorphic and edge case tests |

## Files to Modify

| File | Change |
|---|---|
| `src/main/game/search-state/game_state.h` | Add `recompute_payload_from_scratch()` declaration (debug only) |
| `src/main/game/search-state/game_state.cpp` | Implement `recompute_payload_from_scratch()` |
| `src/main/solver/solver.cpp` | Add debug assertion after flat_cache insert |
| `CMakeLists.txt` | Add new files to sources |

## Key Technical Details

- `dual_cache` inherits from `cache_interface`, wraps both `lru_cache` and `flat_cache`
- The solver will treat `dual_cache` like a flat_cache (no iterators, no live bits) — this is fine for testing cache encoding correctness
- `recompute_payload_from_scratch()` should be guarded by `#ifndef NDEBUG`
- The assertion in solver.cpp should also be `#ifndef NDEBUG`
- Use `compact_state::matches()` to compare payloads (it compares bytes 3-31, ignoring occupied flag and depth)
- Large cache capacity (e.g., 10,000,000) ensures no evictions for most seeds, enabling full pre-eviction agreement testing
- For FreeCell and Klondike dual_cache tests, use 30s timeout and only 5 seeds — these games can be slow

## Important: Implementing `recompute_payload_from_scratch()`

This method must rebuild the compact_state from the current game state. Look at `init_payload_and_hash()` in `game_state.cpp` to understand how the payload is initially built. Your recomputation should follow the same logic but without the Zobrist hash part. Key steps:

1. Zero a fresh `compact_state`
2. Set foundation tops (or hole top for hole games) from the foundation/hole piles
3. Set waste pointer from waste pile size
4. For each card in each pile, determine its descriptor:
   - Cards in foundations → skip (they're tracked via foundation tops)
   - Cards in cells → `IN_CELL`
   - Cards in hole → `IN_HOLE`
   - Bottom card of a tableau pile → `ROOT`
   - Card with a face-down card below it or at position 0 in original deal → `STARTING` or `STARTING_FACE_UP`
   - Card on top of another card → `PARENT_0` through `PARENT_3` based on the parent card's ID via `parent_table`
5. The exact logic depends on the game type — study `init_payload_and_hash()` carefully

**If implementing the full recomputation is too complex**, a simpler alternative: compute the Zobrist hash from scratch (XOR all the individual Z_card/Z_found/Z_waste values) and compare against `get_zobrist_hash()`. This is a weaker check but still catches most incremental update bugs.

## How to Build and Test

```bash
# Release build + tests
./build.sh --release --unit-tests
cd cmake-build-release
ctest -R unit_tests --output-on-failure
ctest -R regression_level1 --output-on-failure
ctest -R regression_level2 --output-on-failure
ctest -R regression_level3 --output-on-failure

# Debug build (slower, enables assertions)
./build.sh --debug --unit-tests
cd cmake-build-debug && ctest -R unit_tests --output-on-failure
```

## What NOT to Do

- Do NOT modify `flat_cache.h` or `flat_cache.cpp`
- Do NOT modify the solver DFS logic (except adding the debug assertion)
- Do NOT change any existing test files
- Do NOT add performance optimizations
- Do NOT modify `cache_interface.h`

## Completion Criteria

1. Regression Levels 1–3 all pass
2. All dual_cache tests pass (no disagreements)
3. Debug payload recomputation assertion passes in debug build
4. Edge case tests pass
5. Code compiles cleanly with `-Wall -Wextra -Werror`
6. Commit your work with descriptive commit messages

When done, summarise what you implemented, confirm all tests pass, and report any issues found.
