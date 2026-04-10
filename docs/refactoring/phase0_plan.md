# Phase 0: Metamorphic Testing Infrastructure (COMPLETED)

**Date:** 2026-04-10
**Status:** ✓ COMPLETE
**Scope:** Zobrist hash + compact_state payload inline undo. Excludes predecessor/accordion.
**Branch:** `feature/refactoring-phases`
**Related:** `descriptor_undo_analysis.md` (in this directory)

---

## Overview

Phase 0 sets up metamorphic testing infrastructure so that every change in Phase 1 is validated against the existing code as oracle.

The goal is to build a test harness that runs the solver with **dual computation**: the existing undo-stack path computes hash and payload values, and a new inline path (added in Phase 1) computes the same values independently. After each undo operation, the harness asserts that both paths produce bit-identical results.

This harness is in place and passing before Phase 1 code is written.

---

## Phase 0: Metamorphic Testing Infrastructure

### Goal

Build a test harness that runs the solver with **dual computation**: the existing undo-stack path computes hash and payload values, and a new inline path computes the same values independently. After each undo operation, the harness asserts that both paths produce bit-identical results.

This harness must be in place and passing BEFORE any Phase 1 code is written.

### Step 0.1: Add `#ifdef VALIDATE_INLINE_UNDO` scaffolding

Add a preprocessor flag `VALIDATE_INLINE_UNDO` that, when defined, enables validation code in each `undo_*_move` function. Initially the validation code does nothing — it just saves and restores hash/payload values to prove the scaffolding works.

**File:** `src/main/game/search-state/game_state.cpp`

Add to each `undo_*_move` function (except `undo_sequence_move` and `undo_accordion_move` which are out of scope):

```cpp
void game_state::undo_regular_move(const move m) {
#ifdef VALIDATE_INLINE_UNDO
    // Snapshot state BEFORE old undo path runs
    uint64_t expected_hash;
    compact_state expected_payload;
#endif

    // === EXISTING UNDO CODE (unchanged) ===
    zobrist_undo undo = zobrist_undo_stack.back();
    zobrist_undo_stack.pop_back();
    // ... (all existing undo logic) ...
    place_card(m.from, take_card(m.to));

#ifdef VALIDATE_INLINE_UNDO
    // Snapshot state AFTER old undo path — this is the expected result
    expected_hash = zobrist_hash_value;
    expected_payload = payload;

    // === NEW INLINE UNDO CODE (to be filled in Phase 1 steps) ===
    // For now: no-op placeholder
    uint64_t inline_hash = expected_hash;       // placeholder
    compact_state inline_payload = expected_payload; // placeholder

    // === VALIDATE ===
    assert(inline_hash == expected_hash
        && "INLINE UNDO: hash mismatch");
    assert(inline_payload.matches(expected_payload)
        && "INLINE UNDO: payload mismatch");
#endif
}
```

Repeat for `undo_built_group_move`, `undo_stock_k_plus_move`, `undo_stock_to_all_tableau_move`.

**Build command:**
```bash
./build.sh --release --unit-tests
```

Enable with CMake option:
```bash
cmake -DVALIDATE_INLINE_UNDO=ON ..
```

**Implementation notes:**
- The `#ifdef` block must appear AFTER the existing undo code runs (so pile state and hash are already restored)
- The new inline code will gradually be filled in during Phase 1 steps
- The placeholders ensure the scaffolding compiles and the asserts pass trivially

**Status:** ✓ COMPLETED - Commit `39112b2`

### Step 0.2: Add CMake option for validation build

**File:** `CMakeLists.txt`

Add an option:
```cmake
option(VALIDATE_INLINE_UNDO "Enable dual-path Zobrist undo validation" OFF)
if(VALIDATE_INLINE_UNDO)
    add_definitions(-DVALIDATE_INLINE_UNDO)
endif()
```

Also add a new CTest target:
```cmake
add_test(NAME validation_build_unit_tests
    COMMAND ${CMAKE_BINARY_DIR}/unit_tests --gtest_filter=-DualCacheTest*:MismatchDiagnostic*
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

**Status:** ✓ COMPLETED - Included in commit `39112b2`

### Step 0.3: Verify scaffolding passes all existing tests

Run:
```bash
cd cmake-build-release
cmake -DVALIDATE_INLINE_UNDO=ON ..
make -j$(nproc)
ctest -R unit_tests --output-on-failure
ctest -R regression_level1 --output-on-failure
```

All tests must pass. The validation asserts are trivially satisfied by the placeholders.

**Status:** ✓ VERIFIED - All unit tests pass, Level 1 regression passes

### Step 0.4: Add a validation-specific regression test

Add a CTest entry that runs Level 1 regression with the validation build. This is the test that will catch Phase 1 bugs:

```cmake
if(VALIDATE_INLINE_UNDO)
    add_test(NAME validation_regression_level1
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/regression_runner.py
            --solver ${CMAKE_BINARY_DIR}/solvitaire
            --oracle ${CMAKE_SOURCE_DIR}/tests/oracles/level1_oracle.json
            --level 1
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(validation_regression_level1 PROPERTIES TIMEOUT 300)
endif()
```

**Status:** ✓ COMPLETED - Part of CMakeLists.txt

---

## Phase 0 Exit Criteria

- [x] `VALIDATE_INLINE_UNDO` builds without warnings
- [x] All unit tests pass with the flag enabled
- [x] Level 1 regression passes with the flag enabled
- [x] Scaffolding is in all 4 undo functions (regular, built_group, stock_k_plus, stock_to_all_tableau)

---

## Summary

✓ Phase 0 is complete. The validation infrastructure is in place and passing all tests.

The foundation is ready for Phase 1: incremental inline undo implementation.

**Next:** See `phase1_plan.md` for Phase 1 implementation strategy.
