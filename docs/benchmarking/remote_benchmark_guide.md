# Remote Benchmark Guide

How to run the multi-game, multi-cache benchmark suite on a remote many-core machine.

## Overview

`scripts/remote_benchmark.py` runs the solver in parallel across:
- **12 game types** (freecell, klondike variants, accordion, bakers-game, golf, etc.)
- **3 cache configurations**: `auto` (default), `hash-only`, `force-lru` (legacy baseline)
- **150–200 seeds per combination**

On 32 cores this takes roughly 2–4 hours. Results are written to per-combination CSVs
plus a `combined.csv`, then analysed with `analysis/compare_caches.R`.

## Remote Machine Requirements

- Linux (Ubuntu 22.04+) recommended
- 32+ cores, 16+ GB RAM (1TB is fine; the solver uses lazy mmap so physical RAM
  consumed is proportional to states actually visited, not cache capacity)
- Python 3.6+, R (for analysis), CMake 3.10+, Boost program_options
- Filesystem may be wiped periodically — pull and rebuild fresh each session

## Workflow

### Step 1: Setup (first time or after filesystem wipe)

Copy `scripts/setup_remote.sh` to the machine and run it:

```bash
# On the remote machine — clone and build:
bash setup_remote.sh \
    --repo git@github.com:turingfan/ReSolvitaire.git \
    --dir ~/ReSolvitaire-caching
```

Or if the repo is already cloned, just pull and rebuild:

```bash
cd ~/ReSolvitaire-caching
git pull
./build.sh --release
```

The script installs apt/brew dependencies, builds the release binary, runs a smoke
test, and prints the benchmark run command.

### Step 2: Quick validation (optional, ~10 min)

```bash
cd ~/ReSolvitaire-caching
python3 scripts/remote_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --workers 32 --quick \
    --output-dir results/$(date +%Y%m%d)_quick
```

`--quick` runs 5 games × 50 seeds × 3 configs. Useful to confirm the build works
before committing to the full run.

### Step 3: Full benchmark run (~2–4 hours on 32 cores)

```bash
python3 scripts/remote_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --workers 32 \
    --output-dir results/$(date +%Y%m%d)
```

Progress is printed every 5% of tasks. The run is resumable only by restarting
(no checkpoint); if interrupted, partial results are in `combined.csv`.

#### Key options

| Flag | Default | Description |
|---|---|---|
| `--workers N` | all CPUs | Parallel solver processes |
| `--output-dir PATH` | `results/remote` | Directory for CSV output |
| `--quick` | off | 5 games, 50 seeds, shorter timeouts |
| `--games TYPE ...` | all | Run only specified game types |
| `--configs auto hash-only force-lru` | all three | Cache configs to include |
| `--cache-capacity BYTES` | solver default | Override cache size |

### Step 4: Collect results

Run on the remote machine after benchmarks complete:

```bash
bash scripts/collect_results.sh results/$(date +%Y%m%d)
```

This tars the results directory and prints the `scp` command to copy it to your
local machine. **Do this before Sunday's filesystem wipe.**

### Step 5: Analyse locally

```bash
# Untar if needed:
tar xzf benchmark_<hostname>_<timestamp>.tar.gz

# R summary comparing cache configs:
Rscript analysis/compare_caches.R results/20260407/combined.csv
```

The R script prints:
- Outcome % (solved / timed-out) by game × cache config
- Median wall time on solved instances
- Speedup ratios: `auto` vs `force-lru`, `hash-only` vs `auto`
- States searched comparison

## Cache Configurations Compared

| Config name | Solver flags | Description |
|---|---|---|
| `auto` | _(none)_ | Default: predecessor cache for accordion, flat cache for most games, lru fallback |
| `hash-only` | `--cache-type hash-only` | Hash-only flat cache (16-byte clusters, no payload verification) |
| `force-lru` | `--force-lru` | Legacy LRU cache (Boost MultiIndex) — baseline comparison |

## Game Types in Full Run

| Game | Seeds | Timeout | Notes |
|---|---|---|---|
| free-cell | 1–200 | 60s | Fast, well-studied |
| klondike-deal-1 | 1–200 | 60s | Standard Klondike |
| klondike-deal-3-nospace | 1–150 | 60s | Harder Klondike variant |
| bakers-game | 1–150 | 60s | Baker's Game |
| accordion | 1–200 | 60s | Exercises predecessor_flat_cache |
| seahaven-towers | 1–150 | 60s | |
| simple-simon | 1–150 | 60s | |
| golf | 1–200 | 30s | Fast |
| black-hole | 1–200 | 30s | |
| spanish-patience | 1–100 | 120s | Hard; long runs |
| gaps-one-deal | 1–150 | 60s | |
| eight-off | 1–150 | 60s | |

Total: ~7,200 solver invocations × 3 configs = ~21,600 tasks.

## Output Files

```
results/<date>/
  combined.csv              # all results merged
  <game>_<config>.csv       # one CSV per (game, cache config) combination
  run_metadata.json         # timing, config, commit info
```

CSV columns: `game, seed, cache, outcome, states_searched, unique_states,
backtracks, solution_ms, wall_ms, streamliner, error`

## Re-running After a Filesystem Wipe

The remote machine filesystem is cleared each Sunday. The full re-run procedure is:

```bash
# 1. Clone and build (setup_remote.sh is idempotent)
bash <(curl -fsSL https://raw.githubusercontent.com/turingfan/ReSolvitaire/dev/scripts/setup_remote.sh) \
    --repo git@github.com:turingfan/ReSolvitaire.git

# 2. Run
cd ~/ReSolvitaire-caching
python3 scripts/remote_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --workers 32 \
    --output-dir results/$(date +%Y%m%d)

# 3. Collect before Sunday
bash scripts/collect_results.sh results/$(date +%Y%m%d)
```

## Notes

- `remote_benchmark.py` invokes the solver directly rather than via `run_benchmark.py`
  because `run_benchmark.py` does not yet support `--force-lru` / `--cache-type`
  passthrough (parked TODO in `task_benchmark_backwards_compat.md`).
- The solver uses mmap lazy allocation: a 100M-entry cache reserves ~3.2 GB virtual
  address space but only commits physical pages as states are visited. The 1TB machine
  has ample physical and virtual memory.
- CI on `dev` runs unit tests + Level 1 regression on ubuntu-22.04 and macos-latest
  automatically on every push. The remote benchmark is a separate, manually triggered
  performance comparison — not part of CI.
