#!/usr/bin/env python3
"""
remote_benchmark.py — parallel multi-game, multi-cache benchmark orchestrator.

Designed to run on a many-core machine (e.g. 32+ cores, 1TB RAM).
Covers multiple game types across three cache configurations:
  - auto       : default cache selection (flat/predecessor/hash-only based on game)
  - hash-only  : explicit hash_only_cache (--cache-type hash-only)
  - force-lru  : legacy LRU cache (--force-lru), baseline comparison

Usage:
    python3 scripts/remote_benchmark.py \\
        --solver cmake-build-release/bin/solvitaire \\
        --workers 32 \\
        --output-dir results/$(date +%Y%m%d) \\
        [--quick]   # subset of games, fewer seeds

Output:
    results/<date>/<game>_<config>.csv  — per-combination CSVs
    results/<date>/combined.csv         — all results merged
    results/<date>/run_metadata.json    — timing and config info
"""

import argparse
import csv
import json
import multiprocessing
import os
import subprocess
import sys
import time
from datetime import datetime

# ---------------------------------------------------------------------------
# Game configurations
# ---------------------------------------------------------------------------
# Each entry: (game_type, seeds, timeout_ms, streamliner, notes)
# timeout_ms is per-instance wall time limit passed to the solver.
# Streamliner 'smart' retries with 'none' on unsolvable — best for correctness;
# use 'auto-foundations' for speed benchmarks where we just want timings.

GAME_CONFIGS_FULL = [
    # Game type                    Seeds      Timeout  Streamliner   Notes
    ("free-cell",                  (1, 200),  60000,   "auto-foundations", "Fast, well-studied"),
    ("klondike-deal-1",            (1, 200),  60000,   "auto-foundations", "Popular Klondike"),
    ("klondike-deal-3-nospace",    (1, 150),  60000,   "auto-foundations", "Harder Klondike"),
    ("bakers-game",                (1, 150),  60000,   "auto-foundations", "Baker's Game"),
    ("accordion",                  (1, 200),  60000,   "none",             "Uses predecessor cache"),
    ("seahaven-towers",            (1, 150),  60000,   "auto-foundations", "Seahaven Towers"),
    ("simple-simon",               (1, 150),  60000,   "auto-foundations", "Simple Simon"),
    ("golf",                       (1, 200),  30000,   "none",             "Golf (fast)"),
    ("black-hole",                 (1, 200),  30000,   "auto-foundations", "Black Hole"),
    ("spanish-patience",           (1, 100),  120000,  "auto-foundations", "Hard; long runs"),
    ("gaps-one-deal",              (1, 150),  60000,   "none",             "Gaps"),
    ("eight-off",                  (1, 150),  60000,   "auto-foundations", "Eight Off"),
]

# Quick mode: fewer games, fewer seeds, shorter timeouts
GAME_CONFIGS_QUICK = [
    ("free-cell",       (1, 50), 30000, "auto-foundations", ""),
    ("klondike-deal-1", (1, 50), 30000, "auto-foundations", ""),
    ("accordion",       (1, 50), 30000, "none",             ""),
    ("bakers-game",     (1, 50), 30000, "auto-foundations", ""),
    ("golf",            (1, 50), 15000, "none",             ""),
]

# Cache configurations to compare
# (config_name, extra_solver_args)
CACHE_CONFIGS = [
    ("auto",       []),
    ("hash-only",  ["--cache-type", "hash-only"]),
    ("force-lru",  ["--force-lru"]),
]

# ---------------------------------------------------------------------------
# Worker function — runs one (game, config, seed) triple
# ---------------------------------------------------------------------------

def run_one(args):
    """Run a single solver invocation and return a result dict."""
    solver, game_type, seed, timeout_ms, streamliner, cache_name, cache_args, cache_capacity = args

    cmd = [solver, "--json", "--type", game_type, "--random", str(seed),
           "--timeout", str(timeout_ms)]
    if streamliner and streamliner != "none":
        cmd += ["--streamliners", streamliner]
    if cache_capacity:
        cmd += ["--cache-capacity", str(cache_capacity)]
    cmd += cache_args

    start = time.time()
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              timeout=(timeout_ms / 1000) + 10)
        wall_ms = (time.time() - start) * 1000
        stdout = proc.stdout
    except subprocess.TimeoutExpired:
        wall_ms = (time.time() - start) * 1000
        return {
            "game": game_type, "seed": seed, "cache": cache_name,
            "outcome": "timeout", "states_searched": "", "unique_states": "",
            "backtracks": "", "solution_ms": "", "wall_ms": round(wall_ms),
            "streamliner": streamliner, "error": "subprocess_timeout",
        }
    except Exception as e:
        return {
            "game": game_type, "seed": seed, "cache": cache_name,
            "outcome": "error", "states_searched": "", "unique_states": "",
            "backtracks": "", "solution_ms": "", "wall_ms": "",
            "streamliner": streamliner, "error": str(e),
        }

    # Parse JSON output
    result = {
        "game": game_type, "seed": seed, "cache": cache_name,
        "outcome": "", "states_searched": "", "unique_states": "",
        "backtracks": "", "solution_ms": "", "wall_ms": round(wall_ms),
        "streamliner": streamliner, "error": "",
    }
    try:
        data = json.loads(stdout)
        result["outcome"] = data.get("solution_type", "unknown")
        result["states_searched"] = data.get("states_searched", "")
        result["unique_states"] = data.get("unique_states", "")
        result["backtracks"] = data.get("backtracks", "")
        result["solution_ms"] = data.get("time_ms", "")
    except (json.JSONDecodeError, ValueError):
        result["outcome"] = "parse_error"
        result["error"] = stdout[:200] if stdout else proc.stderr[:200]

    return result


CSV_FIELDS = ["game", "seed", "cache", "outcome", "states_searched",
              "unique_states", "backtracks", "solution_ms", "wall_ms",
              "streamliner", "error"]


def main():
    parser = argparse.ArgumentParser(description="Parallel multi-game benchmark orchestrator")
    parser.add_argument("--solver", required=True, help="Path to solvitaire binary")
    parser.add_argument("--workers", type=int, default=multiprocessing.cpu_count(),
                        help="Number of parallel workers (default: all CPUs)")
    parser.add_argument("--output-dir", default="results/remote",
                        help="Directory for output CSVs")
    parser.add_argument("--cache-capacity", type=int, default=None,
                        help="Cache capacity in bytes (default: solver default)")
    parser.add_argument("--quick", action="store_true",
                        help="Quick mode: subset of games, fewer seeds")
    parser.add_argument("--games", nargs="+",
                        help="Run only these game types (overrides quick/full list)")
    parser.add_argument("--configs", nargs="+",
                        choices=["auto", "hash-only", "force-lru"],
                        default=["auto", "hash-only", "force-lru"],
                        help="Cache configs to run")
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)

    game_configs = GAME_CONFIGS_QUICK if args.quick else GAME_CONFIGS_FULL
    if args.games:
        game_configs = [g for g in game_configs if g[0] in args.games]
        if not game_configs:
            # Build ad-hoc configs for requested games
            game_configs = [(g, (1, 50), 60000, "auto-foundations", "") for g in args.games]

    cache_configs = [(n, a) for n, a in CACHE_CONFIGS if n in args.configs]

    # Build task list
    tasks = []
    for game_type, (seed_lo, seed_hi), timeout_ms, streamliner, _ in game_configs:
        for cache_name, cache_args in cache_configs:
            for seed in range(seed_lo, seed_hi + 1):
                tasks.append((
                    args.solver, game_type, seed, timeout_ms, streamliner,
                    cache_name, cache_args, args.cache_capacity
                ))

    total = len(tasks)
    print(f"[{datetime.now():%H:%M:%S}] Starting {total} tasks on {args.workers} workers")
    print(f"  Games: {[g[0] for g in game_configs]}")
    print(f"  Configs: {[c[0] for c in cache_configs]}")
    print(f"  Output: {args.output_dir}/")

    start_time = time.time()
    metadata = {
        "start_time": datetime.now().isoformat(),
        "solver": args.solver,
        "workers": args.workers,
        "total_tasks": total,
        "game_configs": [(g[0], g[1], g[2], g[3]) for g in game_configs],
        "cache_configs": [c[0] for c in cache_configs],
        "cache_capacity": args.cache_capacity,
    }

    # Open per-combination CSV files
    csv_writers = {}
    csv_files = {}
    combined_path = os.path.join(args.output_dir, "combined.csv")
    combined_file = open(combined_path, "w", newline="")
    combined_writer = csv.DictWriter(combined_file, fieldnames=CSV_FIELDS)
    combined_writer.writeheader()

    def get_writer(game, cache):
        key = (game, cache)
        if key not in csv_writers:
            safe_game = game.replace("/", "-")
            path = os.path.join(args.output_dir, f"{safe_game}_{cache}.csv")
            f = open(path, "w", newline="")
            w = csv.DictWriter(f, fieldnames=CSV_FIELDS)
            w.writeheader()
            csv_writers[key] = w
            csv_files[key] = f
        return csv_writers[key]

    # Run in parallel
    done = 0
    report_interval = max(1, total // 20)  # report every 5%

    with multiprocessing.Pool(args.workers) as pool:
        for result in pool.imap_unordered(run_one, tasks, chunksize=4):
            get_writer(result["game"], result["cache"]).writerow(result)
            combined_writer.writerow(result)
            combined_file.flush()
            done += 1
            if done % report_interval == 0 or done == total:
                elapsed = time.time() - start_time
                rate = done / elapsed
                eta = (total - done) / rate if rate > 0 else 0
                print(f"[{datetime.now():%H:%M:%S}] {done}/{total} done "
                      f"({100*done/total:.0f}%) — "
                      f"{elapsed/60:.1f}m elapsed, ~{eta/60:.1f}m remaining")

    # Close all files
    combined_file.close()
    for f in csv_files.values():
        f.close()

    elapsed = time.time() - start_time
    metadata["end_time"] = datetime.now().isoformat()
    metadata["elapsed_seconds"] = round(elapsed)
    with open(os.path.join(args.output_dir, "run_metadata.json"), "w") as f:
        json.dump(metadata, f, indent=2)

    print(f"\n[{datetime.now():%H:%M:%S}] Done in {elapsed/60:.1f} minutes")
    print(f"Results: {args.output_dir}/")
    print(f"  combined.csv: all {done} results")
    print(f"  Per-combination CSVs: {len(csv_files)} files")


if __name__ == "__main__":
    main()
