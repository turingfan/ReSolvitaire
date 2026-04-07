#!/usr/bin/env python3
"""
benchmark_orchestrator.py — Parallel multi-game, multi-cache benchmark orchestrator.

Wraps the core `run_benchmark.py` script. It partitions the benchmark configurations
and seed ranges into chunks, invoking `run_benchmark.py` across multiple parallel
workers to maximize throughput on many-core machines.

Features:
- Seamless support for `--cache-type hash-only`, `--force-lru`, and `auto` default configurations.
- Filters invalid cache types for specific games (e.g. hash-only on Predecessor games).
- Merges all output chunk CSVs and JSONs automatically.

Usage:
    python3 scripts/benchmark_orchestrator.py \
        --solver cmake-build-release/bin/solvitaire \
        --workers 32 \
        --output-dir results/$(date +%Y%m%d) \
        [--quick]   # subset of games, fewer seeds
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
# Each entry: (game_type, seeds_tuple, timeout_ms, streamliner, notes)
GAME_CONFIGS_FULL = [
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

GAME_CONFIGS_QUICK = [
    ("free-cell",       (1, 50), 30000, "auto-foundations", ""),
    ("klondike-deal-1", (1, 50), 30000, "auto-foundations", ""),
    ("accordion",       (1, 50), 30000, "none",             ""),
    ("bakers-game",     (1, 50), 30000, "auto-foundations", ""),
    ("golf",            (1, 50), 15000, "none",             ""),
]

CACHE_CONFIGS = [
    ("auto",       []),
    ("hash-only",  ["--cache-type", "hash-only"]),
    ("force-lru",  ["--force-lru"]),
]

HASH_ONLY_INELIGIBLE = {
    "accordion",
    "spider",
    "spider-one-suit",
    "spider-two-suits",
}


def run_chunk(args):
    """Worker function: calls run_benchmark.py for a chunk of seeds."""
    run_bench_script, solver, game_type, seed_start, seed_end, timeout_ms, streamliner, cache_name, cache_args, kwargs = args

    # Write chunk data locally, let master stitch them together
    chunk_base = os.path.join(kwargs["output_dir"], f"chunk_{game_type}_{cache_name}_{seed_start}_{seed_end}")
    chunk_csv = f"{chunk_base}.csv"
    chunk_json = f"{chunk_base}.json"

    cmd = [
        sys.executable, run_bench_script,
        "--solver", solver,
        "--type", game_type,
        "--seeds", f"{seed_start}-{seed_end}",
        "--output", chunk_csv,
        "--output-json", chunk_json,
        "--no-summary"
    ]
    if streamliner and streamliner != "none":
        cmd.extend(["--streamliner", streamliner])
    if timeout_ms:
        cmd.extend(["--timeout", str(timeout_ms)])
    if kwargs.get("cache_capacity"):
        cmd.extend(["--cache-capacity", str(kwargs["cache_capacity"])])

    # Important: append label to know which config produced this
    cmd.extend(["--label", cache_name])

    # Add solver passthrough args
    if cache_args:
        cmd.append("--")
        cmd.extend(cache_args)

    try:
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            print(f"[Error] Chunk failed ({game_type} seeds {seed_start}-{seed_end} {cache_name}):", file=sys.stderr)
            print(proc.stderr, file=sys.stderr)
            return {"success": False, "csv": chunk_csv, "json": chunk_json}
    except Exception as e:
        print(f"[Exception] Chunk failed ({game_type}): {e}", file=sys.stderr)
        return {"success": False, "csv": chunk_csv, "json": chunk_json}

    return {"success": True, "csv": chunk_csv, "json": chunk_json}


def chunk_range(start, end, max_chunk_size=10):
    """Yield successive chunk ranges."""
    for i in range(start, end + 1, max_chunk_size):
        yield (i, min(i + max_chunk_size - 1, end))


def merge_csvs(dest_file, csv_files):
    """Merge component CSVs into a single output, retaining one header."""
    header_written = False
    with open(dest_file, "w") as outf:
        for fname in csv_files:
            if not os.path.exists(fname):
                continue
            with open(fname, "r") as inf:
                lines = inf.readlines()
                if not lines:
                    continue
                if not header_written:
                    outf.write(lines[0])
                    header_written = True
                outf.writelines(lines[1:])
            os.remove(fname)


def main():
    parser = argparse.ArgumentParser(description="Parallel multi-game orchestrator for run_benchmark.py")
    parser.add_argument("--solver", required=True, help="Path to solvitaire binary")
    parser.add_argument("--workers", type=int, default=multiprocessing.cpu_count(),
                        help="Number of chunks to run concurrently (default: all CPUs)")
    parser.add_argument("--output-dir", default="results/remote",
                        help="Directory for output CSVs")
    parser.add_argument("--cache-capacity", type=int, default=None,
                        help="Cache capacity in bytes (default: solver default)")
    parser.add_argument("--quick", action="store_true",
                        help="Quick mode: subset of games, fewer seeds")
    parser.add_argument("--games", nargs="+",
                        help="Run only these game types")
    parser.add_argument("--configs", nargs="+",
                        choices=["auto", "hash-only", "force-lru"],
                        default=["auto", "hash-only", "force-lru"],
                        help="Cache configs to run")
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)
    bench_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "run_benchmark.py")
    if not os.path.exists(bench_script):
        print(f"Error: {bench_script} not found.", file=sys.stderr)
        sys.exit(1)

    game_configs = GAME_CONFIGS_QUICK if args.quick else GAME_CONFIGS_FULL
    if args.games:
        game_configs = [g for g in game_configs if g[0] in args.games]
        if not game_configs:
            game_configs = [(g, (1, 50), 60000, "auto-foundations", "") for g in args.games]

    cache_configs = [(n, a) for n, a in CACHE_CONFIGS if n in args.configs]

    chunk_size = 5 if args.quick else 10
    tasks = []
    skipped_combos = []
    
    kwargs = {
        "output_dir": args.output_dir,
        "cache_capacity": args.cache_capacity
    }

    for game_type, (seed_lo, seed_hi), timeout_ms, streamliner, _ in game_configs:
        for cache_name, cache_args in cache_configs:
            if cache_name == "hash-only" and game_type in HASH_ONLY_INELIGIBLE:
                skipped_combos.append((game_type, cache_name))
                continue
            
            for c_start, c_end in chunk_range(seed_lo, seed_hi, chunk_size):
                tasks.append((
                    bench_script, args.solver, game_type, c_start, c_end,
                    timeout_ms, streamliner, cache_name, cache_args, kwargs
                ))
                
    if skipped_combos:
        print(f"  Skipped hash-only for ineligible games: {sorted(set(g for g,_ in skipped_combos))}")

    total = len(tasks)
    print(f"[{datetime.now():%H:%M:%S}] Starting {total} chunks on {args.workers} workers")

    start_time = time.time()
    csv_chunks = []
    
    done = 0
    report_interval = max(1, total // 20)

    with multiprocessing.Pool(args.workers) as pool:
        for result in pool.imap_unordered(run_chunk, tasks):
            if result["csv"]:
                csv_chunks.append(result["csv"])
            done += 1
            if done % report_interval == 0 or done == total:
                elapsed = time.time() - start_time
                rate = done / elapsed
                eta = (total - done) / rate if rate > 0 else 0
                print(f"[{datetime.now():%H:%M:%S}] {done}/{total} chunks done "
                      f"({100*done/total:.0f}%) — "
                      f"{elapsed/60:.1f}m elapsed, ~{eta/60:.1f}m remaining")

    print("Merging chunked results...")
    combined_csv = os.path.join(args.output_dir, "combined.csv")
    merge_csvs(combined_csv, csv_chunks)

    elapsed = time.time() - start_time
    print(f"\n[{datetime.now():%H:%M:%S}] Done in {elapsed/60:.1f} minutes")
    print(f"Results available at: {combined_csv}")

if __name__ == "__main__":
    main()
