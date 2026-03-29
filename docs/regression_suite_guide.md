# Regression Suite Guide

This guide explains how to build ReSolvitaire, run the regression suite, interpret
results, and maintain/extend the test corpus.

---

## Quick Start

```bash
# 1. Build (from project root)
./build.sh --release

# 2. Configure the build directory for testing
cd cmake-build-release && cmake .. && cd ..

# 3. Run the fast regression (Level 1, ~150 instances, < 2 min)
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
```

---

## 1. Building

The project uses CMake. The `build.sh` convenience script handles the most common cases:

```bash
./build.sh                        # release build of solvitaire (default)
./build.sh --release --unit-tests # release build of unit_tests binary
./build.sh --debug                # debug build
```

This generates (or updates) `cmake-build-release/` or `cmake-build-debug/` and places
binaries in `cmake-build-{release,debug}/bin/`.

To build **both** the solver and the unit-test binary in one go:

```bash
cmake -DCMAKE_BUILD_TYPE=Release -Bcmake-build-release -H.
cmake --build cmake-build-release
```

---

## 2. Running Tests with CTest

CTest is the standard way to run the full suite. Always run it from inside the
**build directory** (e.g. `cmake-build-release/`).

```bash
cd cmake-build-release
```

### List all available tests

```bash
ctest -N
```

Expected output:

```
Test #1: trailing_space
Test #2: trailing_space_free_cell
Test #3: regression_level1
Test #4: regression_level2
Test #5: regression_level3
Test #6: regression_level4
Test #7: regression_level5
```

> **Note:** `unit_tests` (the GoogleTest binary) also appears if the `unit_tests`
> binary was built. If it is missing, rebuild with `cmake --build .` from the build
> directory to pick up any CMakeLists.txt changes.

### Run specific tests

```bash
ctest -R unit_tests                  # GoogleTest C++ unit tests (~133 tests)
ctest -R trailing_space              # Output format checks
ctest -R regression_level1           # Level 1 regression (~150 instances, < 2 min)
ctest -R regression_level2           # Level 2 regression (~160 instances, ~5 min)
ctest -R "regression_level[12]"      # Levels 1 and 2 together
ctest -R regression                  # All regression levels
ctest                                # Everything (unit tests + format + regression)
```

### Useful CTest flags

| Flag | Effect |
|------|--------|
| `--output-on-failure` | Print full output only for failing tests |
| `-V` or `--verbose`   | Print full output for all tests |
| `-j <N>`              | Run up to N tests in parallel (use with caution for regression — each level already uses all CPU time) |
| `--rerun-failed`      | Re-run only the tests that failed last time |
| `-R <regex>`          | Run only tests whose name matches the regex |

### Practical examples

```bash
# Quick sanity check after a code change:
ctest -R "unit_tests|regression_level1" --output-on-failure

# Check whether a specific regression level still passes:
ctest -R regression_level2 -V 2>&1 | tee level2_run.log

# Re-run failing tests with full output:
ctest --rerun-failed --output-on-failure
```

### Timeout budget per level

The CTest configuration sets a `--max-instance-timeout-ms` for each level. This is
the hard cap on how long any single instance can run before the solver is killed.
Observed total wall-clock times on a 2026 MacBook Pro (Apple Silicon):

| CTest target        | `--max-instance-timeout-ms` | Observed total |
|---------------------|-----------------------------|--------------------|
| `regression_level1` | (2× baseline; ~30s default) | ~3.5 min |
| `regression_level2` | 60 000 ms (1 min)           | ~1.5 min |
| `regression_level3` | 120 000 ms (2 min)          | ~2.5 min |
| `regression_level4` | 600 000 ms (10 min)         | ~15 min |
| `regression_level5` | 1 800 000 ms (30 min)       | ~1.25 hr |

In practice most instances complete well within their cap. CTest TIMEOUT properties
are set generously (1.5× worst-case) to allow for slow machines without false failures.

---

## 3. Running Tests Directly (without CTest)

You can run the regression runner script directly for more control, for example to
use `--verbose` output or a different timeout cap.

### Level 1 (JSON instance files)

```bash
python3 scripts/regression_runner.py \
    --exe cmake-build-release/bin/solvitaire \
    --instances tests/resources/level1 \
    --oracle tests/oracles/level1.json \
    --verbose
```

### Levels 2–5 (seed-based, no instance files needed)

```bash
python3 scripts/regression_runner.py \
    --exe cmake-build-release/bin/solvitaire \
    --oracle tests/oracles/level2.json \
    --verbose

python3 scripts/regression_runner.py \
    --exe cmake-build-release/bin/solvitaire \
    --oracle tests/oracles/level5.json \
    --max-instance-timeout-ms 43200000   # 12 h cap — override if machine is fast
```

The `--instances` argument is optional for levels 2–5. The runner detects seed-based
oracles (they contain a `baseline_time_ms` field) and invokes `--random <seed>`
directly, bypassing any JSON instance files.

---

## 4. Understanding the Suite

### Suite overview

| Level | Curation set | Target difficulty | Instances | Oracle |
|-------|-------------|-------------------|-----------|--------|
| 1     | —           | < 1 second        | 150       | `tests/oracles/level1.json` |
| 2     | `1m`        | ~1 minute         | 160       | `tests/oracles/level2.json` |
| 3     | `5m`        | ~5 minutes        | 160       | `tests/oracles/level3.json` |
| 4     | `1h`        | ~1 hour           | 160       | `tests/oracles/level4.json` |
| 5     | `6h`        | ~6 hours          | 160       | `tests/oracles/level5.json` |

All oracles are derived from the original Solvitaire experimental dataset. Each oracle
entry records the exact solver configuration (game type or custom rules, streamliner
setting) needed to reproduce the result.

### How levels 1 and 2–5 differ

**Level 1** uses JSON deal files stored in `tests/resources/level1/`. The runner passes
each file directly to the solver. The oracle was generated by running the solver on
those same files, so round-trip consistency is guaranteed.

**Levels 2–5** use `--random <seed>` instead of deal files. This avoids a known bug
in the JSON serialiser (see `docs/known-issues.md`) where exporting and reloading a
deal can produce slightly different node counts. The oracle values come from seed-based
runs in the original experimental dataset, so seed-based invocation matches them exactly.
There are no JSON instance files for levels 2–5 on disk.

### Streamliner settings

Each oracle entry carries a `"streamliner"` field. The runner always passes
`--streamliners <value>` so the solver runs in exactly the same mode as the original
experiment. Possible values are `"none"` and `"both"`.

### Memout exclusion

Instances where the original experiment exhausted the transposition-table cache
(`states_removed_from_cache > 0`) are excluded at curation time. Such instances
may have produced incorrect "unsolvable" verdicts and are unsafe to use as ground truth.

### Missing game types

Spider and Accordion have no verified "unwinnable" instances in the experimental
dataset. All other game types contribute both winnable and unwinnable instances to
every level.

---

## 5. Interpreting Output

### CTest summary

A passing run looks like:

```
Test project /path/to/cmake-build-release
    Start 3: regression_level1
1/1 Test #3: regression_level1 ................   Passed    4.01 sec

100% tests passed, 0 tests failed out of 1
```

A failing run:

```
    Start 3: regression_level1
1/1 Test #3: regression_level1 ................***Failed   12.34 sec

0% tests passed, 1 tests failed out of 1
```

Use `--output-on-failure` or `-V` to see the per-instance details.

### Comparison policy

The runner uses **outcome-only** comparison. The pass/fail rules are:

| Oracle outcome | Actual outcome | Verdict |
|---|---|---|
| SOLVED | SOLVED | **PASS** |
| UNSOLVABLE | UNSOLVABLE | **PASS** |
| SOLVED | UNSOLVABLE | **HARD FAIL** — correctness bug |
| UNSOLVABLE | SOLVED | **HARD FAIL** — correctness bug |
| Any | TIMEOUT | **SOFT PASS** — timing/traversal variance |
| TIMEOUT | SOLVED/UNSOLVABLE | **PASS** — improvement |

`states_searched` counts are stored in oracle files for reference but are not enforced.
Cache implementation changes (pile-ordering removal in M6, future refactors) alter
traversal order and make node counts non-reproducible across refactors.

### Per-instance tags from the regression runner

| Tag | Meaning |
|-----|---------|
| `[OK]` | Outcome matches oracle (with optional node-count note if `--verbose`) |
| `[TIMEOUT/SOFT-PASS]` | Solver timed out — not a failure |
| `[IMPROVED]` | Oracle was TIMEOUT; actual run produced a definitive result |
| `[FAIL]` | Outcome flip: SOLVED↔UNSOLVABLE — correctness bug |
| `[ERROR]` | Runner-level exception (solver crash, bad JSON, etc.) |

### Example verbose output

```
Running Regression: 160 instances (Oracle: level2.json)
------------------------------------------------------------
[OK] american-canister_6993035_winnable.json: solved [nodes: 131205 vs oracle 134033]
[OK] american-canister_1332_unwinnable.json: unsolvable
[TIMEOUT/SOFT-PASS] spanish-patience_seed_6.json (127504328 nodes before timeout; oracle: solved/738 nodes)
Progress: 25/160 (Pass: 25, Fail: 0)
...
------------------------------------------------------------
Final Report: Passed: 160/160
Regression suite components verified successfully.
```

---

## 6. Regenerating Oracles

Regenerate the oracle when:
- You make a change that intentionally alters the search (new pruning, move ordering, etc.)
- You want to add more game types or adjust the difficulty target
- The oracle becomes stale after a solver version bump or system change

### Quick regeneration (in-place update)

The regression runner has a `--regenerate` mode that re-runs all instances in the
existing oracle and overwrites it with fresh values. Metadata (game type, streamliner,
custom rules) is preserved; `states_searched`, `unique_states`, `backtracks`, `max_depth`,
and `solution_type` are updated from the fresh run.

```bash
# Regenerate Level 1 oracle in place
python3 scripts/regression_runner.py \
    --exe cmake-build-release/bin/solvitaire \
    --instances tests/resources/level1 \
    --oracle tests/oracles/level1.json \
    --regenerate \
    --max-instance-timeout-ms 60000

# Regenerate Level 2 oracle in place
python3 scripts/regression_runner.py \
    --exe cmake-build-release/bin/solvitaire \
    --oracle tests/oracles/level2.json \
    --regenerate \
    --max-instance-timeout-ms 60000
```

If any instance fails during regeneration (solver crash, bad output), the oracle is
**not** written. Fix the failure and re-run.

### Full regeneration from scratch (Level 1)

Use `generate_baseline.py` when you need to regenerate the Level 1 oracle from the
original experimental dataset (e.g., after changing the instance set):

```bash
python3 scripts/generate_baseline.py \
    --exe cmake-build-release/bin/solvitaire \
    --instances tests/resources/level1 \
    --output tests/oracles/level1.json \
    --data-dir /path/to/solvitaire-paper-v10-Feb2026
```

The `--data-dir` argument looks up the correct per-instance streamliner from the
experimental dataset. Omitting it runs all instances with `--streamliners none`, which
will produce different node counts for the ~46 smart-run winnable instances.

### Full regeneration from scratch (Levels 2–5)

Curation and oracle generation from the original dataset:

```bash
python3 scripts/curate_test_sets.py --set 1m --data-dir /path/to/dataset
python3 scripts/curate_test_sets.py --set 5m --data-dir /path/to/dataset
python3 scripts/curate_test_sets.py --set 1h --data-dir /path/to/dataset
python3 scripts/curate_test_sets.py --set 6h --data-dir /path/to/dataset
```

Each command writes `tests/oracles/level{2,3,4,5}.json` and curation metadata.

### Verify after regeneration

```bash
cd cmake-build-release
ctest -R "regression_level[12]" --output-on-failure
```

---

## 7. Adding or Changing Test Instances

### Adding a Level 1 instance

1. Produce the JSON deal file with `--deal-only`:
   ```bash
   ./solvitaire --type <game> --random <seed> --deal-only --reveal-hidden > tests/resources/level1/mygame_seed_42.json
   ```
2. Regenerate the Level 1 oracle (see §6).

### Changing difficulty targets

Edit `TARGET_SETS` in `scripts/curate_test_sets.py` to adjust the target solve-time
buckets, then re-run curation for the affected set.

### Supported game types

The full list of ~80 game types is in `scripts/curate_test_sets.py` (`GAMES` list).
Preset game types use `--type <name>`; custom rules use `--custom-rules <path>`.
The `canfield-strict` variant is an example of a custom-rules game — its rules file
is at `tests/rules/canfield-strict.json` and oracle entries reference it via the
`"custom_rules"` field.
