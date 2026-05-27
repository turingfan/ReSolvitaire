# Testing Guide

This guide explains how to build ReSolvitaire, run the full test suite, interpret
results, and maintain/extend the test corpus. For the short version, see
`docs/testing-quickstart.md`.

---

## Quick Start

```bash
# Recommended: unified test driver (builds all 3 configs + runs tests)
python3 scripts/run_tests.py

# Quick check (unit tests only, all 3 configs, ~5 min)
python3 scripts/run_tests.py --quick

# Single gate only
python3 scripts/run_tests.py --gate release --skip-build

# See what would run without executing
python3 scripts/run_tests.py --dry-run
```

Or manually:

```bash
# Gate 1 — Release: build, unit tests, Level 1 regression
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R ^unit_tests$ --output-on-failure
cd cmake-build-release && ctest -R regression_level1 --output-on-failure

# Gate 2 — Trace: build, unit tests, trace targets
./build.sh --trace
cd cmake-build-trace && ctest -R ^unit_tests$ --output-on-failure
cd cmake-build-trace && ctest -R trace_ --output-on-failure

# Gate 3 — Debug: build, unit tests
./build.sh --debug --unit-tests
cd cmake-build-debug && ctest -R ^unit_tests$ --output-on-failure
```

---

## 1. The 3-Gate Model

All three gates must pass before committing. Each gate targets a different
build configuration and catches different classes of failure.

| Gate | Build config | Built by | What it catches |
|------|-------------|----------|-----------------|
| 1 — Release | `cmake-build-release` | `./build.sh --release --unit-tests` | Correctness, performance regression, variant-binary parity |
| 2 — Trace | `cmake-build-trace` | `./build.sh --trace` | Search-trace determinism vs reference binary |
| 3 — Debug | `cmake-build-debug` | `./build.sh --debug --unit-tests` | UB (undefined behaviour), assertion failures |

### What each gate runs

**Gate 1 (release):**
- `ctest -R ^unit_tests$` — GoogleTest suite (~200 tests including integration)
- `ctest -R regression_level1` — Level 1 regression (150 instances, ~2 min)
  - This also matches variant targets: `regression_level1_flat`, `_hash_only`, `_lru`
- Higher levels via `--level N` with `run_tests.py` or manual `ctest -R regression_levelN`

**Gate 2 (trace):**
- `ctest -R ^unit_tests$` — same GoogleTest suite (SearchTrace tests active here too)
- `ctest -R trace_` — trace identity, timeout, and regression targets

**Gate 3 (debug):**
- `ctest -R ^unit_tests$` — GoogleTest suite only (no regression targets in debug)

### Build configurations

| Directory | `SOLVITAIRE_SEARCH_TRACE`? | Compiler flags | What's unique |
|---|---|---|---|
| `cmake-build-release` | No (on main binaries) | `-O3 -flto -DNDEBUG` | Standard release; variant regression tests |
| `cmake-build-trace` | Yes (on all binaries) | `-O3 -flto -DNDEBUG` | Trace variant binaries + CTest trace targets |
| `cmake-build-debug` | No (on main binaries) | `-O0 -g` | Debug symbols; catches UB/assert failures |

**Important:** The `unit_tests` binary is always compiled with `SOLVITAIRE_SEARCH_TRACE=ON`
in all three build configs. This means `SearchTraceTest.*` and `SearchTraceAgreementTest.*`
(the metamorphic hash-only-vs-flat test) run in every gate. These GTest-level trace tests
validate the trace *writer* logic and cache agreement. The CTest `trace_*` targets are a
different thing — they validate trace *identity* against reference binaries and only exist
in the trace build. See §9 for details.

---

## 2. The Unified Test Driver (`run_tests.py`)

`scripts/run_tests.py` wraps the 3-gate workflow. Building is integral — each gate
builds its own config before running tests.

```
run_tests.py                    # all 3 gates, unit tests + level 1 regression
run_tests.py --gate release     # gate 1 only
run_tests.py --gate trace       # gate 2 only
run_tests.py --gate debug       # gate 3 only
run_tests.py --quick            # unit tests only, all 3 configs (~5 min)
run_tests.py --level 2          # unit tests + regression through level N (release gate)
run_tests.py --skip-build       # assume binaries already built
run_tests.py --dry-run          # print commands without executing
run_tests.py --verbose          # pass -V to ctest
```

The driver stops on the first gate failure and prints a summary. Use `--dry-run`
to preview the exact `build.sh` and `ctest` commands that would be executed.

---

## 3. Running Tests with CTest

CTest is the underlying test runner. Always run it from inside the **build directory**.

### List all available tests

```bash
cd cmake-build-release && ctest -N
```

Expected output (release build):

```
Test  #1: trace_tests_not_available (Disabled)
Test  #2: unit_tests
Test  #3: compare_binaries
Test  #4: trailing_space
Test  #5: trailing_space_free_cell
Test  #6: regression_level1
Test  #7: regression_level2
Test  #8: regression_level3
Test  #9: regression_level4
Test #10: regression_level5
Test #11: regression_level1_flat
...
Test #25: regression_level5_lru

Total Tests: 25
```

The `trace_tests_not_available (Disabled)` entry is a placeholder — trace CTest targets
only exist in the trace build (`cmake-build-trace`). In a trace build, `ctest -N` shows
the trace targets instead.

Expected output (trace build):

```
Test  #1: unit_tests
Test  #2: compare_binaries
Test  #3: trailing_space
Test  #4: trailing_space_free_cell
Test  #5: trace_identity_flat
Test  #6: trace_identity_lru
Test  #7: trace_until_timeout
Test  #8: trace_regression_level1
Test  #9: trace_regression_level2

Total Tests: 9
```

### Run specific tests

```bash
ctest -R ^unit_tests$                # GoogleTest C++ suite
ctest -R trailing_space              # Output format checks
ctest -R regression_level1           # Level 1 regression (main + variants)
ctest -R ^regression_level1$         # Level 1 main binary only (anchored)
ctest -R regression_level1_flat      # Level 1 flat variant only
ctest -R "regression_level[12]"      # Levels 1 and 2 together
ctest -R regression                  # All regression levels
ctest -R trace_                      # All trace targets (trace build only)
```

Use `ctest -R ^unit_tests$` (anchored regex) — the unanchored form may match other
targets in future.

### Useful CTest flags

| Flag | Effect |
|------|--------|
| `--output-on-failure` | Print full output only for failing tests |
| `-V` or `--verbose`   | Print full output for all tests |
| `-j <N>`              | Run up to N tests in parallel (use with caution — each regression level already uses all CPU) |
| `--rerun-failed`      | Re-run only the tests that failed last time |
| `-R <regex>`          | Run only tests whose name matches the regex |
| `-N`                  | List tests without running them |

---

## 4. The GoogleTest Suite (`unit_tests`)

The `unit_tests` binary contains all C++ tests: both unit tests and integration tests.
It is built by `./build.sh --release --unit-tests` (or `--debug --unit-tests` or `--trace`).

### Test categories inside unit_tests

**Unit tests** (~17 files) — test individual components:
- `card_test`, `pile_test`, `deal_parser_test` — data structures
- `legal_move_gen_test`, `built_group_move_gen_test`, `face_up_cards_test` — move generation
- `foundations_dominance_test`, `k_plus_stock_test` — dominance heuristics
- `zobrist_test` — Zobrist hashing (comprehensive, ~860 lines)
- `generic_flat_cache_test`, `global_cache_test`, `predecessor_cache_test` — cache correctness
- `solver_cache_selection_test` — cache policy dispatch
- `search_trace_test` — trace file format and semantics
- `search_trace_agreement_test` — metamorphic: hash-only vs flat cache agreement (50 seeds)

**Integration tests** (~13 files) — per-game solvability:
- One file per game type (klondike, free-cell, accordion, spider, black-hole, etc.)
- Each has 4 tests: SimpleSolvable, ComplexSolvable, SimpleUnsolvable, ComplexUnsolvable
- Runs the full solver on JSON deal files in `tests/resources/unit_tests/`

**Always compiled with `SOLVITAIRE_SEARCH_TRACE=ON`**, so trace-related tests
(`SearchTraceTest.*`, `SearchTraceAgreementTest.*`) run in all three build configs.

---

## 5. Regression Testing (Levels 1–5)

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

### Running regression tests directly

```bash
# Level 1 (JSON instance files)
python3 scripts/regression_runner.py \
    --exe cmake-build-release/bin/solvitaire \
    --instances tests/resources/level1 \
    --oracle tests/oracles/level1.json \
    --verbose

# Levels 2–5 (seed-based, no instance files needed)
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

### Timeout budget per level

The CTest configuration sets a `--max-instance-timeout-ms` for each level. This is
the hard cap on how long any single instance can run before the solver is killed.
Observed total wall-clock times on a 2026 MacBook Pro (Apple Silicon):

| CTest target        | `--max-instance-timeout-ms` | Observed total |
|---------------------|-----------------------------|--------------------|
| `regression_level1` | (2x baseline; ~30s default) | ~3.5 min |
| `regression_level2` | 60 000 ms (1 min)           | ~1.5 min |
| `regression_level3` | 120 000 ms (2 min)          | ~2.5 min |
| `regression_level4` | 600 000 ms (10 min)         | ~15 min |
| `regression_level5` | 1 800 000 ms (30 min)       | ~1.25 hr |

In practice most instances complete well within their cap. CTest TIMEOUT properties
are set generously (1.5x worst-case) to allow for slow machines without false failures.

---

## 6. Variant Regression Testing

The release build includes three variant binaries, each compiled with a single cache
policy forced at compile time:

| Binary | Compile flag | Cache policy | Eligible games |
|---|---|---|---|
| `solvitaire-flat` | `SOLVITAIRE_FLAT_ONLY` | Flat (descriptor Zobrist, 64-byte clusters) | Single-deck, no suit-symmetry; accordion via predecessor |
| `solvitaire-hash-only` | `SOLVITAIRE_HASH_ONLY` | Hash-only (16-byte clusters) | Same as flat |
| `solvitaire-lru` | `SOLVITAIRE_LRU_ONLY` | LRU (Boost MultiIndex, pile-order canonical) | Suit-symmetry, 2-deck, sequence; flat-eligible via `--force-lru` |

### CTest targets

15 variant targets: `regression_level{1..5}_{flat,hash_only,lru}`.

All use `--skip-ineligible` so ineligible games (e.g., suit-symmetry games on the flat
binary) are logged as SKIP rather than failing.

**Per-variant oracles** exist for levels 1–3 (`level{1,2,3}_{flat,hash_only,lru}.json`).
These contain per-variant node counts generated from the `pre-refactor-work` tagged
binary with the appropriate `--cache-type` flag. Levels 4–5 use the default oracle
with `--compare-outcome-only` (node counts not validated — too expensive to generate).

The LRU variant uses `--force-lru` to route flat-eligible games through the LRU cache.
Node counts will differ from the default oracle, so LRU uses its own per-variant oracles.

### Running variant tests

```bash
# Via CTest (from cmake-build-release/)
ctest -R regression_level1_flat --output-on-failure
ctest -R regression_level1_hash_only --output-on-failure
ctest -R regression_level1_lru --output-on-failure

# All level 1 variants at once
ctest -R "regression_level1_(flat|hash_only|lru)" --output-on-failure
```

Variant targets are suppressed in the trace build (variant binaries are not built by
`./build.sh --trace`).

---

## 7. Interpreting Output

### CTest summary

A passing run:

```
Test project /path/to/cmake-build-release
    Start 6: regression_level1
1/1 Test #6: regression_level1 ................   Passed    5.80 sec

100% tests passed, 0 tests failed out of 1
```

A failing run:

```
    Start 6: regression_level1
1/1 Test #6: regression_level1 ................***Failed   12.34 sec

0% tests passed, 1 tests failed out of 1
```

Use `--output-on-failure` or `-V` to see per-instance details.

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

`states_searched` counts are stored in oracle files for reference but are not enforced
by default. Cache implementation changes (pile-ordering removal, future refactors)
alter traversal order and make node counts non-reproducible. Use `--enforce-node-counts`
with `regression_runner.py` when you expect exact node-count match.

### Per-instance tags from the regression runner

| Tag | Meaning |
|-----|---------|
| `[OK]` | Outcome matches oracle (with optional node-count note if `--verbose`) |
| `[TIMEOUT/SOFT-PASS]` | Solver timed out — not a failure |
| `[IMPROVED]` | Oracle was TIMEOUT; actual run produced a definitive result |
| `[FAIL]` | Outcome flip: SOLVED<->UNSOLVABLE — correctness bug |
| `[ERROR]` | Runner-level exception (solver crash, bad JSON, etc.) |

---

## 8. Regenerating Oracles

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

## 9. Trace Testing

The trace build (`cmake-build-trace`, built with `./build.sh --trace`) instruments the
DFS solver to emit structured one-line-per-event text logs. This enables three types of
testing beyond outcome-level regression: identity checks, timeout-boundary checks, and
reference-binary comparison.

### Trace-enabled binaries

`./build.sh --trace` produces four binaries in `cmake-build-trace/bin/`:

| Binary | Cache policy | Use |
|---|---|---|
| `solvitaire-trace` | Default (flat/LRU/predecessor by game type) | Reference binary comparison |
| `solvitaire-flat-trace` | Flat only (`SOLVITAIRE_FLAT_ONLY`) | Identity and timeout tests |
| `solvitaire-lru-trace` | LRU only (`SOLVITAIRE_LRU_ONLY`) | Identity tests |
| `solvitaire-hash-only-trace` | Hash-only | (available; not used in CTest targets) |

### Generating a trace

```bash
./cmake-build-trace/bin/solvitaire-trace \
    --type klondike --random 1 --trace /tmp/run.trace
```

The trace file has a 5-line header (skipped by comparison tools) followed by one event
per line. Events: `QUERY`, `HIT`, `MISS`, `INSERT`, `EVICT`, `LEGAL`, `MOVE`, `UNDO`,
`DOM`, `DEPTH`, `SOLVED`/`UNSOLV`/`TIMEOUT`.

### Debugging divergence

**Break at a specific event** — stop the solver at event N, print the game state to stdout, and exit:

```bash
./cmake-build-trace/bin/solvitaire-flat-trace \
    --type klondike --random 1 --trace /tmp/run.trace --trace-break-at 500
```

The game state (and Zobrist hash, for flat/hash-only policies) is printed when the
break fires.  The trace file is complete up to that event.

**Find the first insertion of a specific hash** — halt and print the game state the
first time a new-state MISS is recorded with a given Zobrist hash (hex, `0x` prefix):

```bash
./cmake-build-trace/bin/solvitaire-flat-trace \
    --type klondike --random 1 --trace /tmp/run.trace \
    --trace-find-hash 0x715820e6e00760fa
```

This is useful for investigating trace divergence.  Typical workflow:

1. Find the operation number where the two traces diverge (using `compare_traces.py`).
2. Run `--trace-break-at N` on the flat binary to get the hash of the diverging state.
3. Run `--trace-find-hash <hash>` on both flat and hash-only to find when each binary
   first saw that hash.  If they fire at different operations, the caches diverged due
   to eviction.  If only one fires, it is a genuine Zobrist hash collision.

### Comparing two traces with `compare_traces.py`

`compare_traces.py` supports three modes: file comparison, binary comparison, and
batch regression.

**File mode** — compare two existing trace files:

```bash
# Full comparison (every event must match)
python3 scripts/compare_traces.py --full /tmp/a.trace /tmp/b.trace

# Stop comparison at first TIMEOUT event
python3 scripts/compare_traces.py --until-timeout 60000 /tmp/a.trace /tmp/b.trace

# Stop comparison at first EVICT event (before cache eviction divergence)
python3 scripts/compare_traces.py --until-evict /tmp/a.trace /tmp/b.trace
```

**Binary mode** — run solver binaries and compare their traces:

```bash
# Run same binary twice (-- separates script flags from solver args)
python3 scripts/compare_traces.py --full \
    --binary cmake-build-trace/bin/solvitaire-flat-trace \
    -- --type klondike --random 1

# Two different binaries
python3 scripts/compare_traces.py --full \
    --binary-a cmake-build-trace/bin/solvitaire-trace \
    --binary-b /path/to/reference/solvitaire-trace \
    -- --type klondike --random 1
```

**Regression mode** — batch comparison across all oracle instances:

```bash
# Level 1 (150 instances, no timeout)
python3 scripts/compare_traces.py --regression \
    --level 1 \
    --binary-a /path/to/reference/solvitaire-trace \
    --binary-b cmake-build-trace/bin/solvitaire-trace \
    --tests-dir tests

# Level 2 (160 instances, 5s solver timeout)
python3 scripts/compare_traces.py --regression \
    --level 2 \
    --binary-a /path/to/reference/solvitaire-trace \
    --binary-b cmake-build-trace/bin/solvitaire-trace \
    --tests-dir tests \
    --timeout-ms 5000

# Smoke test (first 10 instances only)
python3 scripts/compare_traces.py --regression \
    --level 1 --instances 10 --verbose \
    --binary-a ... --binary-b ... --tests-dir tests
```

Regression mode uses streaming O(1)-memory comparison and stops at the first TIMEOUT
event in either trace (wall-clock speed may differ between binaries).

Exit code 0 = identical; 1 = divergence (first differing line printed).

### CTest trace targets (run from `cmake-build-trace/`)

```bash
cd cmake-build-trace

# Unit tests including SearchTraceTest.* and SearchTraceAgreementTest.*
ctest -R ^unit_tests$ --output-on-failure

# All trace_ targets at once
ctest -R trace_ --output-on-failure
```

| CTest target | What it checks | Binary |
|---|---|---|
| `trace_identity_flat` | Same seed run twice -> identical trace | `solvitaire-flat-trace` |
| `trace_identity_lru` | Same seed run twice -> identical trace | `solvitaire-lru-trace` |
| `trace_until_timeout` | Timed-out instance: trace matches up to TIMEOUT event | `solvitaire-flat-trace` |
| `trace_regression_level1` | 150 Level 1 instances vs reference binary | `solvitaire-trace` |
| `trace_regression_level2` | 160 Level 2 instances vs reference binary (5s timeout) | `solvitaire-trace` |

`trace_regression_level1/2` require reference binaries — see below.

### Reference binaries

Reference binaries are stored in `../../05-Executables/reference/` (outside the repo):

| File | Platform | Purpose |
|---|---|---|
| `solvitaire-trace-reference-mac-arm64` | macOS ARM64 | Trace regression on macOS |
| `solvitaire-trace-reference-linux-amd64` | Linux (ARM64 container) | Trace regression in container |

The reference binary is the `solvitaire-trace` binary built from the commit when the
trace infrastructure was first established. If the search logic hasn't changed, the
current binary must produce byte-identical traces. Any divergence is a regression.

**If the reference binary is lost:** rebuild from the established commit, or use the
current binary to establish a new baseline (record the commit hash).

### Linux trace testing via container

```bash
# Build and run trace identity + timeout tests (no reference binary needed)
./scripts/container-build.sh --trace-test

# Run trace_regression_level1 against Linux reference binary
# (05-Executables/reference/solvitaire-trace-reference-linux-amd64 must exist)
./scripts/container-build.sh --trace-regression

# Extract solvitaire-trace binary from container (to establish/update Linux reference)
./scripts/container-build.sh --extract-trace-binary
# Then copy to reference location:
cp solvitaire-trace-linux-amd64 \
   ../../05-Executables/reference/solvitaire-trace-reference-linux-amd64
```

### Agreement tests (`SearchTraceAgreementTest`)

`src/test/unit_tests/search_trace_agreement_test.cpp` (compiled into `unit_tests`,
active only in the trace build via `SOLVITAIRE_SEARCH_TRACE`) contains:

- **`HashOnlyVsFlat_Klondike50Seeds`**: runs hash-only and flat policies on 50 Klondike
  seeds and compares traces **until the first `EVICT` event in either trace**.

  **Why until-evict:** both policies use the same Zobrist hash and the same game-state
  encoding (`skip_pile_ordering=true`), so their HIT/MISS decisions must agree exactly
  as long as neither cache has evicted anything.  After the first eviction, the two
  policies have different cluster sizes (16-byte hash-only vs 64-byte flat) and will
  displace different entries at different times — the search paths legitimately diverge.
  Comparing beyond that point would produce false failures.

  **What it still catches:** a Zobrist hash collision (hash-only HIT where flat says
  MISS) would appear as a line mismatch *before* any EVICT and would correctly fail the
  test.  This is expected to be extremely rare at the search depths used here.

Flat vs LRU comparison is **not** possible in independent runs — LRU canonicalises
tableau pile order while flat does not, so the two search trees diverge immediately.
See `docs/known-issues.md`.

---

## 10. Adding or Changing Test Instances

### Adding a Level 1 instance

1. Produce the JSON deal file with `--deal-only`:
   ```bash
   ./solvitaire --type <game> --random <seed> --deal-only --reveal-hidden > tests/resources/level1/mygame_seed_42.json
   ```
2. Regenerate the Level 1 oracle (see §8).

### Changing difficulty targets

Edit `TARGET_SETS` in `scripts/curate_test_sets.py` to adjust the target solve-time
buckets, then re-run curation for the affected set.

### Supported game types

The full list of ~80 game types is in `scripts/curate_test_sets.py` (`GAMES` list).
Preset game types use `--type <name>`; custom rules use `--custom-rules <path>`.
The `canfield-strict` variant is an example of a custom-rules game — its rules file
is at `tests/rules/canfield-strict.json` and oracle entries reference it via the
`"custom_rules"` field.

---

## 11. Container Testing (Linux)

The `dev` branch includes a `Dockerfile` and `scripts/container-build.sh` for building
and testing on Linux. The script auto-detects `container` CLI (recommended), `docker`,
or `podman`.

```bash
# Build only
./scripts/container-build.sh

# Release: unit tests + Level 1 regression + variant regressions
./scripts/container-build.sh --test
./scripts/container-build.sh --regression
./scripts/container-build.sh --variants

# Trace: unit_tests + trace_identity + trace_until_timeout
./scripts/container-build.sh --trace-test

# Trace regression (needs Linux reference binary in 05-Executables/reference/)
./scripts/container-build.sh --trace-regression

# Extract Linux trace reference binary from container image
./scripts/container-build.sh --extract-trace-binary

# Interactive shell
container run --rm -it solvitaire-dev bash
```

Memory limit for test runs is `-m 7g` due to `flat_cache` mmap virtual address reservation.

**Stale build context (container CLI v0.9):** The `container` CLI's BuildKit builder
maintains its own context cache that persists across builds. If a build uses stale
source files despite `--no-cache`, the fix is:

```bash
container builder delete --force
# Then rebuild normally:
./scripts/container-build.sh --test
```

This destroys and recreates the BuildKit container, clearing all cached contexts.
The next build will be slower (full `apt install`) but will see current source files.

---

## 12. Script Reference

| Script | Purpose | When to use |
|---|---|---|
| `scripts/run_tests.py` | Unified 3-gate test driver | Day-to-day testing |
| `scripts/regression_runner.py` | Regression harness (levels 1–5) | CTest-invoked; also direct use for `--regenerate` |
| `scripts/compare_traces.py` | Trace comparison (file/binary/regression modes) | CTest-invoked; also direct use for debugging |
| `scripts/generate_baseline.py` | Level 1 oracle from experimental dataset | Rare: only when rebuilding Level 1 from scratch |
| `scripts/curate_test_sets.py` | Level 2–5 oracle curation | Rare: only when rebuilding Levels 2–5 from scratch |
| `scripts/check_output_format.py` | Trailing space check | CTest-invoked |
| `scripts/compare_binaries.sh` | Variant binary outcome parity | CTest-invoked |
| `scripts/container-build.sh` | Linux container build/test | Cross-platform CI |
