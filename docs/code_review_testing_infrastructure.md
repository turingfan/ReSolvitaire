# Code Review: `testing-infrastructure` Branch vs `master`

**Reviewer:** Claude (automated deep-dive review)
**Date:** 2026-03-19
**Scope:** All 35 commits on `testing-infrastructure` not on `master`
**Stats:** 362 files changed, 33 927 insertions, 433 deletions

---

## Executive Summary

The `testing-infrastructure` branch adds a substantial, well-designed regression testing
framework to ReSolvitaire. The core concepts are sound: JSON machine-readable output,
a diverse 5-level regression corpus, oracle-driven correctness checking, and memout
exclusion. However, the review uncovered **5 bugs** (2 confirmed broken in CI, 1 stdout
contamination, 1 known upstream bug left unfixed, 1 uninitialized variable risk) and
several quality/maintenance issues that should be resolved before merging to `master`.

### Verdict

**Good to merge after fixes.** The architectural direction is correct but several
concrete defects need addressing first.

---

## 1. C++ Source Changes

### 1.1 `json_helper.cpp` — RapidJSON Refactor (GOOD, with caveats)

**Change:** Replaced `Boost.PropertyTree` JSON writing with direct `RapidJSON::Writer`
calls. Removed 6 helper methods (`piles_to_ptree`, `pile_to_ptree`, `card_to_ptree`).

**Good points:**
- Eliminates Boost PropertyTree dependency for JSON writing (PropertyTree is known to
  produce non-standard JSON — e.g., wrapping all values in strings)
- Direct Writer gives precise control over array/object structure
- Correctly handles sparse piles (cells, reserve, accordion) by emitting `""` for empty
  slots, maintaining 1:1 positional correspondence with game state arrays
- `reveal_hidden` parameter is a clean, non-breaking addition (defaults to `false`)

**Issues:**

- **BUG (upstream, documented):** Line 90 iterates `gs.tableau_piles` (runtime-reordered)
  instead of `gs.original_tableau_piles` (fixed construction order). This was present in
  the original Solvitaire too, but the refactor preserved the bug rather than fixing it.
  The workaround (seed-based regression for levels 2–5) is pragmatic but the fix is
  trivial — change `gs.tableau_piles` → `gs.original_tableau_piles`. **Should be fixed
  now**, not deferred, since it's a one-line change with clear semantics.

- **Minor:** `cout << sb.GetString() << endl` writes directly to stdout. The original
  code used `write_json(cout, pt)` which did the same, so this is consistent. But for
  testability, a `print_game_state_as_json(ostream&, ...)` overload would be preferable.

### 1.2 `card.cpp` / `card.h` — Hidden Card Reveal (GOOD)

**Change:** `card::to_string(bool reveal_hidden)` uses lowercase suit letters (`s`, `h`,
`c`, `d`) for face-down cards when `reveal_hidden=true`, otherwise returns `"##"`.

**Good points:**
- Clean, backwards-compatible API (default `reveal_hidden=false`)
- Lowercase convention matches the existing Solvitaire input parser, which already
  accepts lowercase suits — so round-tripping works without parser changes
- No behavioral change when `reveal_hidden` is not used

**No issues found.**

### 1.3 `deal_parser.cpp` — Schema Hardening (GOOD)

**Change:** Added `cardoremp` and `cardarraywithempty` schema definitions. Added empty
string guards in `parse_cells`, `parse_reserve`, `parse_accordion`.

**Good points:**
- Prevents `stoi` crashes when encountering empty strings in sparse pile arrays
- Schema allows `""` in cells, reserve, accordion — correct for games with empty slots
- `anyOf` with `"AS"` in sequences handles the Gaps ambiguity correctly

**No issues found.**

### 1.4 `command_line_helper.cpp/.h` — New CLI Options (GOOD, minor issue)

**Change:** Added `--json`, `--reveal-hidden`, and `--debug` options.

**Good points:**
- Non-breaking additions — existing CLI behavior unchanged
- `const` getters (correct)

**Issue:**

- **RISK: Uninitialized booleans.** The three new fields (`json_output`, `reveal_hidden`,
  `debug`) are declared as plain `bool` members with no in-class initializer and no
  constructor initializer list entry. They are assigned in `parse()`, but if `parse()`
  returns early (e.g., on `--help` or `--version`), the fields remain uninitialized.
  Accessing them then is **undefined behavior**. Should add `= false` initializers.

### 1.5 `main.cpp` — JSON Output Mode (GOOD, one BUG)

**Change:** Added `--json` output path in `solve_game()`. Suppresses LOG_INFO messages
in JSON mode. Passes `instance_name` through.

**Good points:**
- Clean JSON schema with all key metrics
- Correctly selects streamliner_solution when `run_again` is true
- `instance_name` provides traceability

**BUG: Stdout contamination in JSON + smart-solvability mode.**

When `--streamliners smart-solvability` is used and the streamliner fails, line 181:
```cpp
if (run_again)
    if (!clh.get_classify()) cout << "Unsolvable using streamliner. Running again...\n";
```
prints a human-readable message to stdout **before** the JSON object. The guard checks
`!get_classify()` but not `!get_json_output()`. This corrupts the JSON output.

**Confirmed empirically:**
```
$ solvitaire --type alpha-star --random 3 --json --streamliners smart-solvability
Unsolvable using streamliner. Running again...
{"instance_name":"seed_3","solution_type":"unsolvable",...}
```

**Fix:** Add `&& !clh.get_json_output()` to the guard on line 181.

### 1.6 `solvability_calc.cpp` — Minor Fixes (GOOD)

**Change:** Removed `#include <omp.h>` (not used on macOS), replaced `using boost::optional`
with explicit `boost::optional<>` qualification.

**Good points:**
- Fixes macOS compilation (OpenMP header not available by default)
- More explicit, less error-prone namespace usage

### 1.7 `solver.cpp` — Minor Fix (GOOD)

**Change:** Removed `#include <malloc.h>`.

Correct — `malloc.h` is a Linux-ism; the code doesn't use `malloc()` directly.

### 1.8 Other Minor Changes

- `pile.h`, `game_state.h`: Changed `friend class hasher` → `friend struct hasher`,
  `friend class cached_game_state` → `friend struct cached_game_state`. Correct — these
  are structs, and the mismatch is technically a C++ standard violation.
- `game_state.h`: Added `#include <set>`. Presumably needed by a downstream header or
  future use.

### 1.9 Integration Tests — Path Updates (GOOD)

All 13 integration test files updated path from `resources/X/` to
`tests/resources/unit_tests/X/`. The actual test JSON files (tree hashes identical)
were moved, not modified.

---

## 2. Build System (`CMakeLists.txt`)

### 2.1 Modernization (GOOD)

- **FetchContent for GoogleTest:** Replaces the fragile `configure_file` +
  `execute_process` pattern with `FetchContent_Declare/MakeAvailable`. Pins to
  `v1.14.0` — good practice.
- **CMP0167 policy:** Handles the Boost FindModule deprecation correctly.
- **Compiler-specific warning flags:** Separates GCC-only (`-Wlogical-op`,
  `-Wnoexcept`, `-Wstrict-null-sentinel`) from Clang-compatible flags. Fixes macOS
  build.
- **Apple linker handling:** Skips `-s` and `-Wl,-O1` on macOS (not supported by
  Apple's ld).
- **Source group organization:** Clean categorization into IO/Game/Solver/Evaluation
  groups for IDE support.

### 2.2 Bugs Found

**BUG 1: Missing `unit_tests` CTest target.**

The original `CMakeLists.txt` had:
```cmake
ADD_TEST(NAME unit_tests
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/test"
    COMMAND "${CMAKE_CURRENT_BINARY_DIR}/bin/unit_tests")
```

This was **removed** in the testing-infrastructure branch. The `unit_tests` executable
is still built, but `ctest` will not run the GoogleTest suite. This means CI will not
catch regressions in the 133 C++ unit tests.

**BUG 2: Invalid `--timeout` argument in regression CTest targets.**

The regression targets for levels 2–5 pass `--timeout <N>` to `regression_runner.py`:
```cmake
add_test(NAME regression_level2
    COMMAND ... "--timeout" "60")
```

But `regression_runner.py` does not accept `--timeout`. It accepts
`--max-instance-timeout-ms`. **Confirmed: all level 2–5 CTest targets fail immediately.**

```
regression_runner.py: error: unrecognized arguments: --timeout 60
```

**BUG 3: Invalid `--instances` path for levels 2–5.**

The CTest targets reference `tests/resources/level2` through `level5`, but these
directories were deleted (the JSON instance files were removed as part of the seed-based
workaround). The runner validates the path exists and would fail even if the `--timeout`
issue were fixed.

The docs say to pass any existing directory (e.g., `tests/resources/level1`), but the
CMakeLists was not updated to match.

**BUG 4: Wrong path for `trailing_space_free_cell` test.**

```cmake
"--deal" "${CMAKE_CURRENT_SOURCE_DIR}/tests/resources/free_cell/fc-pro-3.json"
```

The file is actually at `tests/resources/unit_tests/free_cell/fc-pro-3.json`. The test
"passes" because the output check script sees an error message (no trailing spaces in
error output), masking the real failure.

### 2.3 Backwards Compatibility

- **`cmake_minimum_required(VERSION 3.14...4.0)`**: Raised from 3.9.4. FetchContent
  requires 3.14+. This is a **breaking change** for systems with CMake < 3.14. Justified
  — 3.14 is from 2019 and widely available.

- **`target_link_libraries(... PRIVATE ...)`**: Changed from `LINK_PUBLIC`. Correct for
  executables (no downstream consumers).

- **Removed commented-out variant targets** (no-pile-symmetry, no-suit-symmetry, etc.).
  These were unused but could have been valuable for ablation studies. Not a regression
  per se, but worth noting.

---

## 3. Python Scripts

### 3.1 `regression_runner.py` (GOOD overall)

**Good points:**
- Oracle-driven design — the oracle is the single source of truth
- Seed-based invocation for levels 2–5 avoids the JSON round-trip bug
- `normalize_outcome()` handles Solvitaire's output vocabulary
- `--max-instance-timeout-ms` with sensible default
- `[WARN/SLOW]` for timeouts (not a correctness failure) is well-reasoned

**Issues:**
- Seed extraction via `rsplit('_', 2)` is fragile if game types contain underscores
  (e.g., `free-cell-2-cell`). Currently works because game types use hyphens, but a
  regex like `(.+)_(\d+)_(winnable|unwinnable|unsolvable)\.json` would be more robust.

### 3.2 `export_test_deals.py` (ADEQUATE, some concerns)

**Good points:**
- Memout filtering (`removed > 0`) is correctly placed
- Smart vs single-run detection uses authoritative AAA ground truth files

**Concerns:**
- **Outcome determination logic (lines 119–126) duplicates and contradicts earlier logic
  (lines 92–111).** Lines 92–100 set `final_outcome` based on whether run 1 solved. Then
  lines 119–126 re-determine `final_outcome` from `run1_outcome` again, potentially
  overwriting the correct value from lines 104–111 (which looked at run 2). This means
  for smart-run instances where run 1 is unsolvable and run 2 is solved, the second block
  incorrectly sets `final_outcome = "unwinnable"` based on run 1, overwriting the
  `"winnable"` set from run 2 data. **This is a latent bug** — it may not have manifested
  because the curation script already filters to known-outcome instances, but the logic
  is wrong and could produce incorrect oracles if the input data changes.

- **Heuristic fallback** (line 81: `is_smart = len(row) > 15`) is fragile and
  undocumented. Should at minimum log a warning (it does) and ideally be removed in
  favor of strict AAA matching.

### 3.3 `curate_test_sets.py` (ADEQUATE)

- Parallelized with `multiprocessing` — good for performance
- Covers all game types
- Time-based set selection is sensible

### 3.4 `generate_baseline.py` (GOOD)

Simple, focused script for Level 1 oracle generation from JSON files.

### 3.5 `check_output_format.py` (GOOD)

Clean refactor of the original `check_for_no_trailing_space_in_output.py` to use
argparse. Functionally identical.

---

## 4. Test Resources and Oracles

### 4.1 Level 1 Corpus (150 instances) — GOOD

- 15 game types covered
- Mix of solvable and unsolvable instances
- JSON files verified round-trippable
- Oracle generated from JSON-based runs (consistent)

### 4.2 Levels 2–5 Oracles — GOOD (with note)

- Oracle values match seed-based experimental data
- Memout instances excluded from Level 5
- Streamliner and custom_rules metadata embedded in oracle entries

**Note:** The `curated_sets/curated_instances_*.json` files (5000+ lines each) contain
raw curation data including CSV paths to the original experimental dataset. These are
not needed at runtime and add ~22 000 lines to the repo. Consider adding them to
`.gitignore` or moving to a separate data management pipeline.

### 4.3 `tests/rules/canfield-strict.json` — GOOD

Custom rules for canfield-strict regression testing. Correctly defines the game variant.

### 4.4 Regression Results Logs — SHOULD NOT BE COMMITTED

`tests/regression_results/` contains 4 log files from specific test runs. These are
ephemeral artifacts that should not be version-controlled. They add noise to the repo
and will become stale immediately.

---

## 5. Documentation

### 5.1 `docs/known-issues.md` — GOOD

Clear description of the JSON round-trip bug with root cause, confirmed example,
workaround, and proposed fix. Well-structured.

### 5.2 `docs/regression_suite_guide.md` — GOOD (with inaccuracies)

Comprehensive guide. However:
- The CTest examples reference `--timeout` which doesn't exist in the runner
- The "Using the runner script directly" examples work correctly (they don't pass
  `--timeout`)
- Should document that levels 2–5 need a real `--instances` path even though it's unused

### 5.3 `docs/phase1_summary.md` — GOOD

Accurate summary with appropriate caveats about round-trip node count differences.

### 5.4 `docs/testing_infrastructure_log.md` — GOOD

Detailed chronological log of all changes. Useful for understanding decision history.

### 5.5 `docs/solvitaire_results_overview.md` — QUESTIONABLE

This documents exploration of the experimental dataset, not the testing infrastructure
itself. It reads more like session notes than project documentation. Consider removing
or moving to a personal notes directory.

### 5.6 `docs/windows_cheat_sheet.md` — DELETED (was on master)

This file was removed. It contained Windows build instructions. If Windows builds are
still supported, this deletion is a regression. If not, it's fine.

---

## 6. Backwards Compatibility Assessment

| Change | Breaking? | Justified? |
|--------|-----------|------------|
| CMake 3.14 minimum | Yes (was 3.9.4) | Yes — FetchContent requires it, 3.14 is from 2019 |
| Removed `#include <omp.h>` | No (wasn't used) | Yes — fixes macOS build |
| Removed `#include <malloc.h>` | No (wasn't used) | Yes — fixes macOS build |
| Boost PropertyTree no longer used for writing | No (internal) | Yes — RapidJSON is better |
| `card::to_string()` signature change | API change | Backwards compatible (default param) |
| `json_helper::print_game_state_as_json()` signature | API change | Backwards compatible (default param) |
| `solve_game()` gains `instance_name` param | Internal | Not externally visible |
| Test resource paths moved | Build-system | Tests must be run from project root, not `src/test/` |
| `friend class` → `friend struct` | ABI | Technically a fix (original was wrong) |
| Removed Windows cheat sheet | Docs only | May affect Windows users |
| `--json`, `--reveal-hidden`, `--debug` flags added | CLI | Non-breaking additions |

**Overall:** No behavioral regressions in solver correctness. The solver produces
identical results for identical inputs. All breaking changes are in the build system
and are well-justified.

---

## 7. Bug Summary

| # | Severity | Location | Description | Status |
|---|----------|----------|-------------|--------|
| 1 | **HIGH** | `CMakeLists.txt` | `unit_tests` CTest target missing — CI won't run 133 C++ tests | Confirmed |
| 2 | **HIGH** | `CMakeLists.txt` | `--timeout` arg invalid for regression_runner.py — levels 2–5 CTest broken | Confirmed |
| 3 | **MEDIUM** | `CMakeLists.txt` | `--instances` paths for levels 2–5 point to deleted directories | Confirmed |
| 4 | **MEDIUM** | `main.cpp:181` | "Unsolvable using streamliner" message leaks into `--json` stdout | Confirmed |
| 5 | **LOW** | `CMakeLists.txt` | `trailing_space_free_cell` uses wrong path (masked by test design) | Confirmed |
| 6 | **LOW** | `command_line_helper.h` | `json_output`, `reveal_hidden`, `debug` not initialized (UB risk) | Code review |
| 7 | **LOW** | `export_test_deals.py:119-126` | Smart-run outcome logic duplicates/contradicts earlier block | Code review |
| 8 | **INFO** | `json_helper.cpp:90` | Tableau pile serialization uses reordered list (upstream bug, documented) | Known |

---

## 8. Roadmap

### Immediate (before merge to master)

1. **Fix CMakeLists.txt bugs:**
   - Restore `add_test(NAME unit_tests ...)` with working directory set to project root
     (tests use `tests/resources/unit_tests/` paths now)
   - Change `--timeout` to `--max-instance-timeout-ms` for levels 2–5
   - Change `--instances` for levels 2–5 to `tests/resources/level1` (or any existing dir)
   - Fix `trailing_space_free_cell` deal path to `tests/resources/unit_tests/free_cell/fc-pro-3.json`

2. **Fix stdout contamination:**
   - In `main.cpp:181`, add `&& !clh.get_json_output()` to the guard

3. **Initialize bool members:**
   - In `command_line_helper.h`, add `= false` to `json_output`, `reveal_hidden`, `debug`

4. **Fix export_test_deals.py outcome logic:**
   - Remove the duplicate outcome determination block at lines 119–126, or restructure
     so run 2 outcome is not overwritten

### Short-term (next sprint)

5. **Fix the JSON round-trip bug:**
   - In `json_helper.cpp`, change `gs.tableau_piles` → `gs.original_tableau_piles`
   - This is a one-line fix. Once applied, Level 1 oracle should be regenerated from
     seeds for consistency, and JSON instance files could be removed
   - Consider submitting as upstream PR to original Solvitaire

6. **Clean up committed artifacts:**
   - Remove `tests/regression_results/*.log` (ephemeral CI artifacts)
   - Consider `.gitignore` for `tests/resources/curated_sets/` (22K lines of raw data)
   - Remove `docs/solvitaire_results_overview.md` (session notes, not project docs)

7. **Harden seed extraction in regression_runner.py:**
   - Replace `rsplit('_', 2)` with a regex pattern for robustness

### Medium-term

8. **Restore Windows build support:**
   - The removal of `docs/windows_cheat_sheet.md` and the macOS-specific linker changes
     suggest Windows support may have been inadvertently reduced. Verify Windows builds
     still work (or document that Windows is not supported).

9. **Add `--json` output for `--deal-only`:**
   - Currently `--deal-only` writes a game state JSON and `--json` writes a result JSON.
     Consider unifying the output format or adding a schema version field.

10. **Consider removing `--instances` as a required argument for seed-based levels:**
    - The runner requires `--instances` but doesn't use it for levels 2–5. Either make it
      optional or remove the existence check when the oracle indicates seed-based mode.
