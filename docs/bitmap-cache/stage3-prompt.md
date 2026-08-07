# Bitmap Cache Stage 3 — Detailed Specification

This is the detailed reference for Stage 3. The short copyable prompt is in
`stage3-short-prompt.md`.

## Task

Add a `solvitaire-bitmap` variant binary and Level 1 regression oracle. After
this stage, bitmap cache changes are covered by regression tests, matching the
pattern used by flat, hash-only, and lru variants.

## Setup

```bash
git fetch origin
git checkout feature/bitmap-cache && git pull origin feature/bitmap-cache
git checkout -b feature/bitmap-stage3
```

## Before You Write Code

Read these files in order. Do not start coding until you have read all of them.

1. `CLAUDE.md` — build commands, test gates, project rules
2. `docs/bitmap-cache/domain-questions.md` — where to log domain issues
3. `src/main/main.cpp` lines 131-165 — `dispatch_solve()` with its `#if`/`#elif`
   chain for variant binaries (`SOLVITAIRE_LRU_ONLY`, `SOLVITAIRE_FLAT_ONLY`,
   `SOLVITAIRE_HASH_ONLY`) and the `#else` default path
4. `src/main/evaluation/benchmark.cpp` lines 152-220 — `dispatch_run_seed()` and
   `dispatch_run_deal()` — same `#if`/`#elif` variant chain
5. `src/main/evaluation/solvability_calc.cpp` lines 189-217 — `solve_seed()`
   — same variant chain
6. `CMakeLists.txt` lines 260-281 — existing variant binary targets
   (`solvitaire-flat`, `solvitaire-hash-only`, `solvitaire-lru`,
   `solvitaire-mult-scratch`)
7. `CMakeLists.txt` lines 679-708 — existing Level 1 variant regression targets
   (`regression_level1_flat`, `regression_level1_hash_only`,
   `regression_level1_lru`)
8. `src/main/game/cache_interface.h` — `use_bitmap_cache()` eligibility function

## Implementation Steps

### Step 1: Add `SOLVITAIRE_BITMAP_ONLY` dispatch blocks

In **three files**, add a new `#elif defined(SOLVITAIRE_BITMAP_ONLY)` block
after the existing `SOLVITAIRE_HASH_ONLY` block. Each block must:
- Suppress unused parameters with `(void)` casts (match which params each
  function has — not all have `force_lru`)
- Check eligibility with `use_bitmap_cache(rules)`
- Throw `std::runtime_error` if not eligible (this is how `--skip-ineligible`
  detects ineligible games in the regression runner)
- Dispatch to `BitmapPolicy`

#### main.cpp — `dispatch_solve()`

Insert after the `#elif defined(SOLVITAIRE_HASH_ONLY)` block (after line ~145):

```cpp
#elif defined(SOLVITAIRE_BITMAP_ONLY)
    (void)force_lru; (void)cache_type; (void)suit_sym;
    if (!use_bitmap_cache(rules))
        throw std::runtime_error("bitmap-only binary: game not eligible for bitmap cache");
    return solve_game_impl<BitmapPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc);
```

#### benchmark.cpp — `dispatch_run_seed()`

Insert after the `SOLVITAIRE_HASH_ONLY` block:

```cpp
#elif defined(SOLVITAIRE_BITMAP_ONLY)
    (void)force_lru; (void)cache_type; (void)suit_sym;
    if (!use_bitmap_cache(rules))
        throw std::runtime_error("bitmap-only binary: game not eligible for bitmap cache");
    return run_seed_impl<BitmapPolicy>(rules, seed, str_opts, cache_capacity, timeout_ms);
```

#### benchmark.cpp — `dispatch_run_deal()`

Insert after the `SOLVITAIRE_HASH_ONLY` block. Note: this function does NOT
have a `force_lru` parameter, so don't suppress it:

```cpp
#elif defined(SOLVITAIRE_BITMAP_ONLY)
    (void)cache_type; (void)suit_sym;
    if (!use_bitmap_cache(rules))
        throw std::runtime_error("bitmap-only binary: game not eligible for bitmap cache");
    return run_deal_impl<BitmapPolicy>(rules, deal_doc, str_opts, cache_capacity, timeout_ms);
```

#### solvability_calc.cpp — `solve_seed()`

Insert after the `SOLVITAIRE_HASH_ONLY` block. This function uses `stream_opt`
not `str_opts`:

```cpp
#elif defined(SOLVITAIRE_BITMAP_ONLY)
    (void)cache_type; (void)suit_sym;
    if (!use_bitmap_cache(rules))
        throw std::runtime_error("bitmap-only binary: game not eligible for bitmap cache");
    return solve_seed_impl_with_opts<BitmapPolicy>(seed, timeout, rules, cache_capacity, stream_opt);
```

Note: `solve_seed()` does NOT have a `force_lru` parameter.

### Step 2: Add `solvitaire-bitmap` target to CMakeLists.txt

After the `solvitaire-lru` target (around line 273), add:

```cmake
add_executable(solvitaire-bitmap    ${main} ${sources})
target_compile_definitions(solvitaire-bitmap PRIVATE SOLVITAIRE_BITMAP_ONLY)
set_property(TARGET solvitaire-bitmap PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_link_libraries(solvitaire-bitmap PRIVATE ${Boost_LIBRARIES})
```

### Step 3: Build and verify

```bash
./build.sh --release --unit-tests
```

Verify the bitmap binary exists and works:

```bash
./cmake-build-release/bin/solvitaire-bitmap --type klondike --random 1 --json
./cmake-build-release/bin/solvitaire-bitmap --type free-cell --random 1 --json
./cmake-build-release/bin/solvitaire-bitmap --type black-hole --random 1 --json
```

All three should produce valid JSON with a `solution_type`.

Verify ineligible games are rejected:

```bash
./cmake-build-release/bin/solvitaire-bitmap --type accordion --random 1 --json 2>&1
```

This should exit with a non-zero return code and an error message containing
"not eligible".

### Step 4: Generate Level 1 bitmap oracle

Copy the existing Level 1 oracle as a template (the runner needs it to know
which instances to run), then regenerate with the bitmap binary:

```bash
cp tests/oracles/level1.json tests/oracles/level1_bitmap.json

python3 scripts/regression_runner.py \
    --exe cmake-build-release/bin/solvitaire-bitmap \
    --instances tests/resources/level1 \
    --oracle tests/oracles/level1_bitmap.json \
    --skip-ineligible --regenerate
```

This will:
- Run each Level 1 instance through the bitmap binary
- Skip ineligible games (accordion, two-deck, sequence)
- Write bitmap-specific node counts to the oracle

Verify the oracle was generated successfully (the script prints a summary).

### Step 5: Add `regression_level1_bitmap` CTest target

In CMakeLists.txt, inside the `if(NOT SOLVITAIRE_TRACE)` block, after the
existing `regression_level1_lru` target (around line 708):

```cmake
add_test(NAME regression_level1_bitmap
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-bitmap>"
        "--instances" "${CMAKE_CURRENT_SOURCE_DIR}/tests/resources/level1"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level1_bitmap.json"
        "--skip-ineligible" "--enforce-node-counts"
)
set_tests_properties(regression_level1_bitmap PROPERTIES TIMEOUT 900)
```

### Step 6: Run all test gates + bitmap regression

```bash
python3 scripts/run_tests.py
```

All 3 gates must pass.

Then run the new bitmap regression target:

```bash
cd cmake-build-release && ctest -R regression_level1_bitmap --output-on-failure
```

This must also pass.

## Files to Create

| File | How |
|---|---|
| `tests/oracles/level1_bitmap.json` | Generated by Step 4 |

## Files to Modify

| File | Change |
|---|---|
| `src/main/main.cpp` | Add `SOLVITAIRE_BITMAP_ONLY` block in `dispatch_solve()` |
| `src/main/evaluation/benchmark.cpp` | Add blocks in `dispatch_run_seed()` and `dispatch_run_deal()` |
| `src/main/evaluation/solvability_calc.cpp` | Add block in `solve_seed()` |
| `CMakeLists.txt` | Add `solvitaire-bitmap` target + `regression_level1_bitmap` CTest |

Do NOT modify any other files (except `docs/bitmap-cache/domain-questions.md` if
you encounter domain questions).

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

**Commits:** One commit per logical step (e.g. "add dispatch blocks", "add variant
binary", "generate oracle", "add CTest target"). Not one giant commit.

## PR Creation

```bash
git push -u origin feature/bitmap-stage3
gh pr create --base feature/bitmap-cache --title "Stage 3: bitmap variant binary and Level 1 regression" --body "$(cat <<'EOF'
## Summary

- `solvitaire-bitmap` variant binary (`SOLVITAIRE_BITMAP_ONLY` compile flag)
- `SOLVITAIRE_BITMAP_ONLY` dispatch blocks in main.cpp, benchmark.cpp, solvability_calc.cpp
- Level 1 bitmap oracle (`tests/oracles/level1_bitmap.json`)
- `regression_level1_bitmap` CTest target with `--enforce-node-counts`

## Smoke Tests

- [ ] `solvitaire-bitmap --type klondike --random 1 --json` produces valid result
- [ ] `solvitaire-bitmap --type free-cell --random 1 --json` produces valid result
- [ ] `solvitaire-bitmap --type black-hole --random 1 --json` produces valid result
- [ ] `solvitaire-bitmap --type accordion --random 1 --json` exits with error

## Test Results

- [ ] Gate 1 (Release): pass/fail
- [ ] Gate 2 (Trace): pass/fail
- [ ] Gate 3 (Debug): pass/fail
- [ ] `regression_level1_bitmap`: pass/fail

## Domain Questions

(Any entries added to `docs/bitmap-cache/domain-questions.md`)

---
_Generated by Claude Code_
EOF
)"
```
