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
