# Prompt for Gemini: Implement Milestone 3 — Flat Cache

## Project Context

You are working on **ReSolvitaire**, a C++14 DFS solver for solitaire card games. The codebase is in the directory you're running in (or clone from the `refactor-caching` branch).

**Before doing anything, read these files carefully:**
1. `CLAUDE.md` — Build commands, architecture overview, testing instructions
2. `docs/cache-redesign/implementation_plan_v2.md` — The full refactoring plan (Milestones 2-8). Milestone 2 is complete. You are implementing **Milestone 3**.
3. `docs/cache-redesign/milestone3_detailed_plan.md` — **Detailed step-by-step plan for your work.** This is your primary reference. Follow it precisely.

## What Has Been Done (Milestone 2 — Complete)

The codebase already has:

- **`cache_interface`** (`src/main/game/cache_interface.h`): Abstract base class with `insert()`, `contains()`, `clear()`, `size()`, etc. The existing `lru_cache` implements it.
- **`compact_state`** (`src/main/game/compact_state.h/cpp`): 32-byte payload struct with 4-bit per-card descriptors, foundation fields, waste pointer, occupied flag, depth. Has `matches()` for comparison (compares bytes 3-31).
- **`zobrist_hash`** (`src/main/game/zobrist.h/cpp`): Descriptor-aligned Zobrist hash tables. `Z_card[52][16]`, `Z_found[4][14]`, `Z_waste[64]`, `Z_hole_top[52]`.
- **`parent_table`** (`src/main/game/parent_table.h/cpp`): Maps card IDs to parent descriptors.
- **`game_state`** has `get_zobrist_hash()` and `get_payload()` accessors. The hash and payload are incrementally updated in `make_move`/`undo_move`.
- **13 unit tests** for the Zobrist/payload system in `src/test/unit_tests/zobrist_test.cpp`, all passing.

## Your Task: Milestone 3

Implement a **flat, open-addressed hash table** (`flat_cache`) with two-slot clusters as a new implementation of `cache_interface`. This cache is **NOT connected to the solver yet** — it only needs to pass unit tests.

### What to implement:

1. **`src/main/game/flat_cache.h`** — Header for the flat cache class
2. **`src/main/game/flat_cache.cpp`** — Implementation
3. **`src/test/unit_tests/flat_cache_test.cpp`** — Unit tests (7 tests specified in the detailed plan)
4. **`CMakeLists.txt`** — Add the new source files

### Key technical details:

- **Cluster = 2 × compact_state = 64 bytes = 1 cache line.** Use `alignas(64)`.
- **Cluster indexing:** Use multiply-high Fibonacci hashing: `(uint64_t)((__uint128_t)hash * num_clusters >> 64)`
- **Replacement policy (TwoBig1):** Slot 0 = depth-preferred (overwrite only if new depth ≤ stored depth). Slot 1 = always-replace.
- **Empty slot detection:** `compact_state::is_occupied()` checks byte 0.
- **On insert:** Copy the payload, set the occupied flag on the copy, then store it. Do NOT modify the game_state's payload.
- **`insert()` returns true** if state was newly inserted, **false** if already present.
- **`contains()`** checks both slots in the target cluster using `matches()`.

### How to build and test:

```bash
# Build everything including unit tests
./build.sh --release --unit-tests

# Run all unit tests
cd cmake-build-release && ctest -R unit_tests --output-on-failure

# Run all tests (unit + integration)
cd cmake-build-release && ctest --output-on-failure
```

The project uses `-Wall -Wextra -Werror` so all warnings are errors.

### What NOT to do:

- Do NOT modify the solver (`solver.cpp`) — that's Milestone 4
- Do NOT modify `game_state.cpp` or `game_state.h` — Milestone 2 is complete
- Do NOT change any existing tests
- Do NOT modify the `lru_cache` or `global_cache` code
- Do NOT add features beyond what's specified
- Do NOT set depth in the payload during insert — that's Milestone 4's job

### How to generate distinct game states for tests:

```cpp
#include "../game/sol_rules.h"
#include "../game/search-state/game_state.h"
#include "../game/zobrist.h"

// Get FreeCell rules
sol_rules rules = sol_preset_types::get("free-cell");

// Create game states with different seeds — each has unique payload/hash
game_state gs1(rules, 1, game_state::streamliner_options::NONE);
game_state gs2(rules, 2, game_state::streamliner_options::NONE);
```

### Completion criteria:

1. All existing tests still pass (unit tests, integration tests)
2. All 7 new flat_cache tests pass
3. Code compiles cleanly with `-Wall -Wextra -Werror`
4. `flat_cache` correctly implements the `cache_interface`
5. Commit your work with descriptive commit messages

When done, please summarise what you implemented and confirm all tests pass.
