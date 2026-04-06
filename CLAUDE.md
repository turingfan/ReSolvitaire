# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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

```bash
# All unit tests
cd cmake-build-release && ctest -R unit_tests --output-on-failure

# Run a single integration test (e.g., klondike)
cd cmake-build-release && ctest -R klondike --output-on-failure

# Level 1 regression (150 instances, ~2 min)
cd cmake-build-release && ctest -R regression_level1 --output-on-failure

# Levels 2–5 regression (longer; see docs/regression_suite_guide.md)
cd cmake-build-release && ctest -R regression_level2 --output-on-failure
```

Tests are compiled into the `unit_tests` binary (GoogleTest). CTest definitions are in `CMakeLists.txt` lines 225–288.

## Linux / Container Testing

The `dev` branch includes a working `Dockerfile` and helper script for building and testing on Linux without native dependencies.

### Using the container-build.sh script (macOS / container CLI)

```bash
# Build the Linux image
./scripts/container-build.sh

# Build and run unit tests inside the container
./scripts/container-build.sh --test

# Build and run Level 1 regression inside the container
./scripts/container-build.sh --regression

# Interactive shell in container
container run --rm -it solvitaire-dev bash
```

The script auto-detects the available container runtime: `container` CLI (recommended), `docker`, or `podman`.

### Direct Docker / Podman usage

```bash
# Build the image
docker build -t solvitaire-dev .

# Run unit tests
docker run --rm solvitaire-dev \
    bash -c "cd cmake-build-release && ctest -R unit_tests --output-on-failure"

# Run Level 1 regression
docker run --rm solvitaire-dev \
    bash -c "cd cmake-build-release && ctest -R regression_level1 --output-on-failure"

# Interactive shell
docker run --rm -it solvitaire-dev bash
```

**Note:** The Dockerfile uses `ubuntu:22.04` and installs only essential build dependencies (`build-essential`, `cmake`, `libboost-program-options-dev`, `git`, `python3`, `ca-certificates`). The build runs `./build.sh --release` and `./build.sh --release --unit-tests`, including a smoke test of unit tests, before producing the image.

## Running the Solver

```bash
./cmake-build-release/solvitaire --type klondike --random 42 --json
./cmake-build-release/solvitaire --type free-cell instance.json --json
./cmake-build-release/solvitaire --available-game-types
./cmake-build-release/solvitaire --describe-game-rules klondike
```

Key CLI options: `--type`, `--random <seed>`, `--json`, `--reveal-hidden`, `--streamliners {none|auto-foundations|suit-symmetry|both|smart}`, `--cache-capacity <bytes>`, `--timeout <ms>`, `--solvability <N>`.

## Architecture

### Core Flow

1. `main.cpp` parses CLI args via `command_line_helper`, constructs `sol_rules` (from preset or JSON), and dispatches to solver, benchmarking, or solvability modes.
2. `solver.cpp` runs DFS. At each node it calls `game_state::get_dominance_move()` (free simplifications), then `game_state::get_legal_moves()`, then recurses. States are hashed and checked against the transposition table to avoid revisiting.
3. `game_state` manages pile state with copy/restore semantics for backtracking. It holds multiple pile arrays: `foundations`, `tableau`, `cells`, `reserve`, `waste`, `stock`.
4. Cache selection (`cache_interface.h:use_new_cache()`): most single-deck games use `flat_cache` (descriptor-based Zobrist hash, O(1) amortised); two-deck, spider-stock, and suit-symmetry games fall back to `lru_cache` (Boost MultiIndex, pile-order canonicalised).

### Key Classes

| Class | File | Purpose |
|---|---|---|
| `solver` | `src/main/solver/solver.h/cpp` | DFS engine, result reporting |
| `game_state` | `src/main/game/search-state/game_state.h/cpp` | State, move generation, undo |
| `flat_cache` | `src/main/game/flat_cache.h/cpp` | Fast transposition table (descriptor Zobrist hash) |
| `lru_cache` | `src/main/game/global_cache.h/cpp` | Transposition table with LRU eviction (Boost MultiIndex) |
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
- `smart` — try `both`; if unsolvable, retry with `none` (used by regression oracle for hard instances)

## Regression Testing Infrastructure

**5 levels** of regression tests, each backed by an oracle in `tests/oracles/`:

| Level | Instances | Source | Timeout/instance | Total ~time |
|---|---|---|---|---|
| 1 | 150 | JSON files in `tests/resources/level1/` | N/A | <2 min |
| 2 | ~160 | `--random <seed>` | 60s | ~5 min |
| 3 | ~160 | `--random <seed>` | 120s | ~10 min |
| 4 | ~160 | `--random <seed>` | 600s | ~100 min |
| 5 | ~160 | `--random <seed>` | 1800s | ~600 min |

Oracle files are JSON arrays; each entry stores `outcome`, `states_searched`, `backtracks`, and `streamliner`. The Python harness (`scripts/regression_runner.py`) drives CTest, invokes the solver, and compares results. See `docs/regression_suite_guide.md` for the full workflow.

## Known Issues

1. **`json_helper.cpp` line ~90:** Uses `gs.tableau_piles` (runtime-reordered for symmetry) instead of `gs.original_tableau_piles`. This breaks JSON round-trips when symmetry reordering is active. One-line fix is documented but deferred; Levels 2–5 avoid this by using seed-based runs.
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
