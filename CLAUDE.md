# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project-Level Rules

**Read `01-Knowledge-Base/AGENTS.md`** in the project directory for mandatory rules that apply to all agents and all tools. That file is the single source of truth for process rules, where things go, and session-end checklist.

## Repo-Specific Rules

  1. **EVALUATING AGENT WORK:** When asked to evaluate work done by another agent, you MUST: (a) pull the latest code from the remote before reading anything — do not proceed if the pull is blocked, explain why it is essential; (b) read the actual files, do not trust the other agent's summary or report; (c) verify claims (e.g. "Zobrist stripped", "tests pass") by inspecting the code directly, not by accepting the agent's word.

  2. **Completed branch docs** get archived to `01-Knowledge-Base/Archive/` and removed from this repo. Only active branch docs belong in `docs/`.

## Project Overview

ReSolvitaire is a general-purpose DFS solver for perfect-information solitaire games, written in C++14. This fork (`ReSolvitaire-caching`) is a research branch focused on caching optimization and a comprehensive 5-level regression testing infrastructure.

## Build Commands

**Primary Development Branch:** `dev` — cross-platform (macOS + Linux) with full CI/CD.

**Prerequisites:** C++14 compiler, CMake 3.10+, Boost 1.53.0+ (program_options)

```bash
# Release build (default)
./build.sh

# Release build with unit tests binary
./build.sh --release --unit-tests

# Debug build
./build.sh --debug
```

Build outputs go to `cmake-build-release/` or `cmake-build-debug/`.

## Testing

**There are three required test gates. All three must pass before any commit.**

**Quick-start:** See `docs/testing-quickstart.md` for the entry-level guide.

```bash
# All 3 gates (unit tests + level 1 regression) — required before every commit
python3 scripts/run_tests.py

# Fast check (unit tests only, all 3 configs, ~5 min)
python3 scripts/run_tests.py --quick

# Single gate, skip rebuild
python3 scripts/run_tests.py --gate release --skip-build
```

### Gate 1 — Release build

```bash
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R ^unit_tests$ --output-on-failure
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
```

### Gate 2 — Trace build

```bash
./build.sh --trace
cd cmake-build-trace && ctest -R ^unit_tests$ --output-on-failure
cd cmake-build-trace && ctest -R trace_ --output-on-failure
```

The trace build (`cmake-build-trace`) uses `Release + SOLVITAIRE_TRACE=ON`. It runs
`SearchTraceTest.*` and `SearchTraceAgreementTest.*` unit tests plus four CTest targets:
`trace_identity_flat`, `trace_identity_lru`, `trace_until_timeout`,
`trace_regression_level1` (and `trace_regression_level2`). These are **no-ops** in
release and debug builds — only this gate exercises them.

`trace_regression_level1/2` requires reference binaries in
`../../05-Executables/reference/` — see `05-Executables/reference/README.md`.

### Gate 3 — Debug build

```bash
./build.sh --debug --unit-tests
cd cmake-build-debug && ctest -R ^unit_tests$ --output-on-failure
```

### Build configurations

| Directory | Built by | `SOLVITAIRE_SEARCH_TRACE`? | What's unique |
|---|---|---|---|
| `cmake-build-release` | `./build.sh --release --unit-tests` | No | Standard release; variant regression tests |
| `cmake-build-debug` | `./build.sh --debug --unit-tests` | No | Debug symbols; catches UB/assert failures |
| `cmake-build-trace` | `./build.sh --trace` | Yes | Trace variant binaries + SearchTrace* tests |

Use `ctest -R ^unit_tests$` (anchored regex) to avoid matching other targets.

### Trace testing in detail

See `docs/testing-guide.md` §10 for full trace testing documentation,
including `compare_traces.py` (also handles regression mode) and the reference binary system.

**Quick trace comparison (two runs of the same binary):**

```bash
./cmake-build-trace/bin/solvitaire-trace --type klondike --random 1 \
    --trace /tmp/a.trace
./cmake-build-trace/bin/solvitaire-trace --type klondike --random 1 \
    --trace /tmp/b.trace
python3 scripts/compare_traces.py --full /tmp/a.trace /tmp/b.trace
```

**Break at a specific event (for debugging divergence):**

```bash
./cmake-build-trace/bin/solvitaire-trace --type klondike --random 1 \
    --trace /tmp/a.trace --trace-break-at 500
```

## Linux / Container Testing

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

**Note:** The Dockerfile builds all three configurations (release + trace + debug not
included; release and trace only). Memory limit for test runs is `-m 7g` due to
`flat_cache` mmap virtual address reservation.

## Running the Solver

```bash
./cmake-build-release/solvitaire --type klondike --random 42 --json
./cmake-build-release/solvitaire --type free-cell instance.json --json
./cmake-build-release/solvitaire --available-game-types
./cmake-build-release/solvitaire --describe-game-rules klondike
```

Key CLI options: `--type`, `--random <seed>`, `--json`, `--reveal-hidden`, `--streamliners {none|auto-foundations|suit-symmetry|both|smart-solvability}`, `--cache-capacity <bytes>`, `--timeout <ms>`, `--solvability <N>`.

## Architecture

### Core Flow

1. `main.cpp` parses CLI args via `command_line_helper`, constructs `sol_rules` (from preset or JSON), and dispatches to solver, benchmarking, or solvability modes.
2. `solver_impl<Policy>` runs DFS. At each node it calls `game_state::get_dominance_move()` (free simplifications), then `game_state::get_legal_moves()`, then recurses. States are hashed and checked against the transposition table to avoid revisiting. The solver is templated on cache policy — zero virtual dispatch in the DFS hot path.
3. `game_state_impl<Policy>` manages pile state with copy/restore semantics for backtracking. It holds multiple pile arrays: `foundations`, `tableau`, `cells`, `reserve`, `waste`, `stock`. Hash/payload computation is controlled at compile time via `if constexpr (Policy::computes_hash)`.
4. Cache dispatch (`main.cpp:dispatch_solve()`): compile-time policy selection — Accordion games use `PredecessorPolicy` (128-byte clusters, predecessor-encoded state); most single-deck games use `FlatPolicy` (descriptor-based Zobrist, 64-byte clusters) or `HashOnlyPolicy` (hash-only, 16-byte clusters); two-deck, spider-stock, and suit-symmetry games use `LRUPolicy` / `lru_cache` (Boost MultiIndex, pile-order canonicalised). All flat variants use `generic_flat_cache<ClusterPolicy>`.

### Key Classes

| Class | File | Purpose |
|---|---|---|
| `solver_impl<Policy>` | `src/main/solver/solver.h/cpp` | Templated DFS engine, result reporting |
| `game_state_impl<Policy>` | `src/main/game/search-state/game_state.h/cpp` | State, move generation, undo |
| `cache_policy.h` | `src/main/game/cache_policy.h` | Policy structs: `FlatPolicy`, `HashOnlyPolicy`, `PredecessorPolicy`, `LRUPolicy` |
| `generic_flat_cache<P>` | `src/main/game/generic_flat_cache.h` | Templated flat transposition table (cluster size varies by policy) |
| `lru_cache` | `src/main/game/global_cache.h/cpp` | Transposition table with LRU eviction (Boost MultiIndex) |
| `platform_memory.h` | `src/main/game/platform_memory.h` | mmap lazy allocation RAII (`platform::lazy_buffer`) |
| `sol_rules` | `src/main/game/sol_rules.h/cpp` | Game rule enums (build policy, space policy, etc.) |
| `card` | `src/main/game/card.h/cpp` | Card value, suit, face-down flag |
| `pile` | `src/main/game/pile.h/cpp` | Vector-based card stack; `pile[0]` = top |
| `move` | `src/main/game/move.h/cpp` | Move type enum + from/to/count fields |

### Move Generation & Dominance

- `game_state.legal_moves.cpp` — generates all valid moves per game rules
- `game_state.dominance_moves.cpp` — auto-foundation and symmetry heuristics that prune search without losing completeness
- `game_state.pile_order.cpp` — canonicalizes tableau pile order for LRU cache deduplication (skipped for flat cache games)

### Face-Down Card Encoding

Face-down cards are encoded with **lowercase** suit letters in JSON (`"as"` = face-down Ace of Spades; `"AS"` = face-up). The deal parser already accepts this. `--reveal-hidden` exposes the actual hidden cards.

### Streamliner Modes

- `none` — brute-force DFS
- `auto-foundations` — auto-move cards that can only go to foundations
- `suit-symmetry` — break suit symmetry in Klondike tableau
- `both` — combine the above two
- `smart-solvability` — try `both`; if unsolvable, retry with `none` (used by regression oracle for hard instances)

## Regression Testing Infrastructure

**5 levels** of regression tests, each backed by an oracle in `tests/oracles/`:

| Level | Instances | Source | Timeout/instance | Total ~time |
|---|---|---|---|---|
| 1 | 150 | JSON files in `tests/resources/level1/` | N/A | <2 min |
| 2 | ~160 | `--random <seed>` | 60s | ~5 min |
| 3 | ~160 | `--random <seed>` | 120s | ~10 min |
| 4 | ~160 | `--random <seed>` | 600s | ~100 min |
| 5 | ~160 | `--random <seed>` | 1800s | ~600 min |

Oracle files are JSON arrays; each entry stores `outcome`, `states_searched`, `backtracks`, and `streamliner`. The Python harness (`scripts/regression_runner.py`) drives CTest, invokes the solver, and compares results. See `docs/testing-guide.md` for the full workflow.

## Known Issues

1. **~~`json_helper.cpp` JSON round-trip~~:** RESOLVED. `json_helper.cpp` now correctly uses `gs.original_tableau_piles`. See `docs/known-issues.md` issue #1.
2. **Flat cache + suit-symmetry:** The flat cache cannot provide suit-canonical deduplication; games using `--streamliners suit-symmetry` or `both` automatically fall back to `lru_cache`. See `docs/known-issues.md` issues #3 and #4.

## Compilation Flags

- **Release:** `-O3 -flto -DNDEBUG`, strict warnings: `-pedantic -Wall -Wextra -Werror`
- **Debug:** `-O0 -g`
- External deps: Boost (system install), RapidJSON (header-only in `lib/`), GoogleTest (CMake FetchContent v1.14.0)

## Benchmarking

Run benchmarks via the Python orchestration script (branch: `benchmark-python`):

```bash
# Run on 150 seeds, write CSV
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-150 --timeout 60000 \
    --output results/current.csv

# Quick R summary (also runs automatically at end of run_benchmark.py)
Rscript analysis/summary.R results/current.csv

# Full comparison report
Rscript analysis/benchmark.R \
    --baseline results/baseline.csv \
    --current  results/current.csv \
    --output   results/comparison.html
```

See `docs/benchmarking/active/quickstart.md` for more.
