#!/usr/bin/env python3
"""
benchmark_orchestrator.py — Parallel multi-game, multi-solver benchmark orchestrator.

Wraps the core `run_benchmark.py` script.  It partitions benchmark
configurations and seed ranges into chunks, then drives them via GNU
``parallel`` across memory-bounded workers.

Usage — variant binaries (recommended):
    python3 scripts/benchmark_orchestrator.py \\
        --solver-dir cmake-build-release/bin \\
        --output-dir results/$(date +%Y%m%d)

    # Default scope is GAME_CONFIGS_QUICK.  For the full 14-game matrix:
    python3 scripts/benchmark_orchestrator.py \\
        --solver-dir cmake-build-release/bin --full \\
        --output-dir results/$(date +%Y%m%d)

Usage — single binary with cache-type flags (legacy):
    python3 scripts/benchmark_orchestrator.py \\
        --solver cmake-build-release/bin/solvitaire \\
        --output-dir results/$(date +%Y%m%d)

Concurrency
-----------
Workers are bounded by a memory-aware cap (D2/D3):

    max_safe = floor(total_RAM × 0.80 / 3 GB)
    jobs     = min(requested_or_default, 64, max_safe)

Each chunk also has a hard ceiling (T2) to prevent a wedged run_benchmark.py
from hanging a worker forever:

    chunk_ceiling = ceil(seeds_in_chunk × solver_timeout_s × 1.5 × CHUNK_MARGIN)

where CHUNK_MARGIN = 1.5 (outer scheduling slack on top of the per-seed
1.5× grace already built into bench_lib.process).  This is conservative: the
per-seed 1.5× is enforced *inside* run_benchmark.py so the chunk ceiling is
really just a last-resort backstop.

On overrun the whole run_benchmark.py process group is SIGTERM'd then
SIGKILL'd via bench_lib.process.run_with_deadline; the chunk is recorded as
failed; the pool continues.

Guard rails (T5)
----------------
- The full GAME_CONFIGS_FULL matrix (14 × 4 solvers × 500 seeds × 20-min
  timeout) is EXPLICIT OPT-IN via ``--full``.  The default scope is
  GAME_CONFIGS_QUICK (5 games, 50 seeds, 30 s).
- ``--dry-run`` prints the planned job count, memory budget, and command list
  without executing anything (does not require built binaries or ``parallel``).
"""

import argparse
import csv
import json
import math
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time
from datetime import datetime

# ---------------------------------------------------------------------------
# Add scripts/ to sys.path so bench_lib is importable when run from project root
# ---------------------------------------------------------------------------
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench_lib.process import run_with_deadline
from bench_lib.concurrency import (
    JobsResult,
    compute_jobs,
    effective_memory_limit,
    planning_worker_bytes,
    DEFAULT_CAPACITY_ENTRIES,
)

# Map orchestrator solver labels to a cache type for memory sizing.
# "default" (auto-dispatch) is treated as multiplicity — the largest flat-family
# footprint — so the estimate is conservative.
_LABEL_CACHE_TYPE = {
    "default": "multiplicity",
    "flat": "flat",
    "hash-only": "hash-only",
    "lru": "lru",
}

# ---------------------------------------------------------------------------
# Game configurations
# ---------------------------------------------------------------------------
# Each entry: (game_type, seeds_tuple, timeout_ms, streamliner, notes)
GAME_CONFIGS_FULL = [
    ("somerset",                   (1, 500),  1200000,   "none", "about 50% solvable"),
    ("free-cell",                  (1, 500),  1200000,   "none", "well-studied"),
    ("klondike",                   (1, 500),  1200000,   "none", "Classic Klondike"),
    ("klondike-deal-1",            (1, 500),  1200000,   "none", "Klondike Variant"),
    ("klondike-deal-3-nospace",    (1, 500),  1200000,   "none", "Harder Klondike"),
    ("bakers-game",                (1, 500),  1200000,   "none", "Baker's Game"),
    ("accordion",                  (1, 500),  1200000,   "none",             "Uses predecessor cache"),
    ("seahaven-towers",            (1, 500),  1200000,   "auto-foundations", "Seahaven Towers"),
    ("simple-simon",               (1, 500),  1200000,   "auto-foundations", "Simple Simon"),
    ("golf",                       (1, 500),  1200000,   "none",             "Golf (fast)"),
    ("black-hole",                 (1, 500),  1200000,   "auto-foundations", "Black Hole"),
    ("spanish-patience",           (1, 500),  1200000,  "auto-foundations", "Hard; long runs"),
    ("gaps-one-deal",              (1, 500),  1200000,   "none",             "Gaps"),
    ("eight-off",                  (1, 500),  1200000,   "auto-foundations", "Eight Off"),
]

GAME_CONFIGS_QUICK = [
    ("free-cell",       (1, 50), 30000, "auto-foundations", ""),
    ("klondike-deal-1", (1, 50), 30000, "auto-foundations", ""),
    ("accordion",       (1, 50), 30000, "none",             ""),
    ("bakers-game",     (1, 50), 30000, "auto-foundations", ""),
    ("golf",            (1, 50), 15000, "none",             ""),
]

# ---------------------------------------------------------------------------
# Solver configurations
# ---------------------------------------------------------------------------
# In --solver-dir mode each entry maps a label to:
#   (binary_name, extra_solver_args, skip_ineligible)
# binary_name is looked up in --solver-dir.
# skip_ineligible=True passes --skip-ineligible to run_benchmark.py so
# ineligible game/solver combos are skipped gracefully rather than erroring.
#
# In legacy --solver mode, LEGACY_CACHE_CONFIGS is used instead.
SOLVER_CONFIGS = [
    ("default",   "solvitaire",           [],              False),
    ("flat",      "solvitaire-flat",      [],              True),
    ("hash-only", "solvitaire-hash-only", [],              True),
    ("lru",       "solvitaire-lru",       ["--force-lru"], True),
]

# Legacy mode: single binary, different flag combinations.
LEGACY_CACHE_CONFIGS = [
    ("auto",       [],                          False),
    ("hash-only",  ["--cache-type", "hash-only"], True),
    ("force-lru",  ["--force-lru"],               False),
]

# ---------------------------------------------------------------------------
# Chunk timeout formula (T2)
# ---------------------------------------------------------------------------
# The solver's --timeout is a CPU-time budget; under load a well-behaved run
# may take up to SOLVER_WALL_CAP_MULT × that in WALL time before the solver's
# own wall safety-cap fires. So the per-seed WALL allowance must be the wall
# ceiling, not 1.5× — otherwise a heavily-descheduled (but correct) chunk is
# killed prematurely, the very failure the CPU-time budget exists to avoid.
# CHUNK_MARGIN is a further outer buffer for per-chunk startup/teardown.
#
# chunk_ceiling_s = ceil(seeds_in_chunk × solver_timeout_s × SOLVER_WALL_CAP_MULT × CHUNK_MARGIN)
#
# This is deliberately a SAFETY ceiling (catch a wedged run_benchmark.py), not
# an expected runtime: idle, each seed finishes at ~its CPU budget (CPU≈wall)
# and the chunk returns as soon as run_benchmark exits.
CHUNK_MARGIN: float = 1.5

# Must match the solver's default --wall-cap-mult (see run_benchmark._WALL_CAP_MULT
# and command_line_helper). The solver self-terminates by wall_cap_mult × timeout.
SOLVER_WALL_CAP_MULT: float = 10.0

# Minimum chunk ceiling in seconds (prevent absurdly short timeouts for quick games)
MIN_CHUNK_CEILING_S: float = 120.0


def chunk_ceiling_s(seeds_in_chunk: int, timeout_ms: int) -> float:
    """Compute the per-chunk hard ceiling in seconds.

    Formula:
        ceiling = max(MIN_CHUNK_CEILING_S,
                      ceil(seeds × (timeout_ms / 1000) × SOLVER_WALL_CAP_MULT × CHUNK_MARGIN))

    The inner SOLVER_WALL_CAP_MULT factor is the solver's wall safety-cap as a
    multiple of its CPU-time --timeout: a correct run can use that much WALL time
    under load before self-terminating, so the chunk ceiling must allow it.
    CHUNK_MARGIN is an additional outer scheduling buffer.
    """
    per_seed_wall_s = (timeout_ms / 1000.0) * SOLVER_WALL_CAP_MULT
    ceiling = math.ceil(seeds_in_chunk * per_seed_wall_s * CHUNK_MARGIN)
    return max(MIN_CHUNK_CEILING_S, float(ceiling))


# ---------------------------------------------------------------------------
# Per-chunk runner (T2)
# ---------------------------------------------------------------------------

def run_chunk(cmd, chunk_csv, chunk_json, label, timeout_s):
    """Run one run_benchmark.py chunk with a hard wall-clock ceiling.

    Uses bench_lib.process.run_with_deadline so the entire process group
    (run_benchmark.py + /usr/bin/time + solver) is killed together on overrun.

    Parameters
    ----------
    cmd:
        Full command list for run_benchmark.py.
    chunk_csv:
        Expected output CSV path (used to detect whether partial output exists).
    chunk_json:
        Expected output JSON path.
    label:
        Human-readable chunk label for log messages.
    timeout_s:
        Hard ceiling in seconds for this chunk.

    Returns
    -------
    dict with keys: success (bool), csv (str), json (str).
    """
    result = run_with_deadline(
        cmd,
        solver_timeout_s=timeout_s,  # ceiling in seconds; 1.5× is already inside
        sigterm_grace_s=30.0,
    )

    if result.disposition in ("KILLED_AFTER_SIGTERM", "KILLED_HARD"):
        print(
            f"[KILLED] Chunk {label} overran {timeout_s:.0f}s ceiling "
            f"(disposition={result.disposition}) — recording as failed.",
            file=sys.stderr,
        )
        if result.stdout.strip():
            print(
                f"  Partial stdout ({len(result.stdout)} chars) discarded "
                f"(chunk CSV may be incomplete).",
                file=sys.stderr,
            )
        return {"success": False, "csv": chunk_csv, "json": chunk_json}

    if result.returncode != 0:
        print(
            f"[Error] Chunk failed ({label}): exit {result.returncode}",
            file=sys.stderr,
        )
        if result.stderr.strip():
            print(result.stderr, file=sys.stderr)
        return {"success": False, "csv": chunk_csv, "json": chunk_json}

    return {"success": True, "csv": chunk_csv, "json": chunk_json}


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

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


def require_parallel(dry_run: bool) -> str:
    """Return the path to GNU parallel, or exit with an actionable error.

    In --dry-run mode this check is skipped (parallel is not needed to show
    the plan).
    """
    if dry_run:
        return "parallel"  # placeholder — not actually invoked
    path = shutil.which("parallel")
    if path is None:
        print(
            "ERROR: GNU parallel is not on PATH.\n"
            "Install it with:\n"
            "  macOS:  brew install parallel\n"
            "  Debian/Ubuntu: apt-get install parallel\n"
            "  RHEL/CentOS:   yum install parallel  (or dnf install parallel)\n"
            "Then re-run.",
            file=sys.stderr,
        )
        sys.exit(1)
    return path


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Parallel multi-game orchestrator for run_benchmark.py",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Concurrency:
  Workers default to min(cpu_count//2, memory_safe_cap).  Pass --workers N to
  override; a warning is printed if N exceeds the memory-safe ceiling, but the
  run proceeds at the safe number.

  Memory-safe cap: floor(total_RAM × 0.80 / 3 GB) — based on ~3 GB resident
  per worker (solver + Python + /usr/bin/time).

Guard rails:
  --full         Opt-in to the full GAME_CONFIGS_FULL matrix (14 games, 500 seeds,
                 20-min timeout).  Default scope is GAME_CONFIGS_QUICK.
  --dry-run      Print the planned job count, memory budget, and command list;
                 do not execute anything.  Does not require built binaries or
                 GNU parallel to be installed.

Chunk timeout (T2):
  Each chunk has a hard ceiling:
    ceiling = max(120 s, ceil(seeds × timeout_ms/1000 × 1.5 × 1.5))
  On overrun the entire process group is SIGTERM'd → SIGKILL'd; the chunk is
  recorded as failed; the pool continues.  Pass --chunk-timeout to override.
""",
    )

    # Primary: multi-binary mode
    parser.add_argument("--solver-dir", default=None,
                        help="Directory containing variant binaries (solvitaire, solvitaire-flat, "
                             "solvitaire-hash-only, solvitaire-lru). Each found binary is run as "
                             "a separate labelled configuration.")
    parser.add_argument("--solvers", nargs="+",
                        choices=["default", "flat", "hash-only", "lru"],
                        default=None,
                        help="Subset of solver variants to run (default: all found in --solver-dir). "
                             "Ignored in legacy --solver mode.")

    # Legacy: single binary with flag variants
    parser.add_argument("--solver", default=None,
                        help="Path to a single solvitaire binary (legacy mode). "
                             "Use --solver-dir instead to benchmark variant binaries.")
    parser.add_argument("--configs", nargs="+",
                        choices=["auto", "hash-only", "force-lru"],
                        default=["auto", "hash-only", "force-lru"],
                        help="Cache configurations for legacy --solver mode.")

    # Scope
    parser.add_argument("--full", action="store_true",
                        help="OPT-IN to the full GAME_CONFIGS_FULL matrix "
                             "(14 games × 500 seeds × 20-min timeout). "
                             "Default scope is GAME_CONFIGS_QUICK (5 games, 50 seeds, 30 s).")
    parser.add_argument("--games", nargs="+",
                        help="Run only these game types")
    parser.add_argument("--seeds", default=None,
                        help="Override seed range as N-M (e.g. 1-100)")
    parser.add_argument("--timeout", type=int, default=None,
                        help="Override timeout per instance in ms")

    # Concurrency
    parser.add_argument("--workers", type=int, default=None,
                        help="Number of parallel workers (default: memory-aware safe value). "
                             "A warning is printed if this exceeds the memory-safe ceiling.")
    parser.add_argument("--chunk-timeout", type=int, default=None,
                        help="Hard ceiling per chunk in seconds (default: computed from seeds × timeout).")

    # Output
    parser.add_argument("--output-dir", default="results/remote",
                        help="Directory for output CSVs")
    parser.add_argument("--cache-capacity", type=int, default=None,
                        help="Cache capacity in bytes (default: solver default)")

    # Dry run
    parser.add_argument("--dry-run", action="store_true",
                        help="Print the planned job count, memory budget, and command list "
                             "without executing anything.")

    args = parser.parse_args()

    dry_run = args.dry_run

    if not dry_run and not args.solver_dir and not args.solver:
        print("Error: either --solver-dir or --solver is required", file=sys.stderr)
        sys.exit(1)

    # ---------------------------------------------------------------------------
    # Check GNU parallel is available (skip in dry-run)
    # ---------------------------------------------------------------------------
    parallel_bin = require_parallel(dry_run)

    # ---------------------------------------------------------------------------
    # Memory-aware job count (D2/D3)
    # ---------------------------------------------------------------------------
    # Cache types this run will exercise (for the per-worker memory estimate).
    if args.solver_dir:
        wanted = set(args.solvers) if args.solvers else {lbl for lbl, *_ in SOLVER_CONFIGS}
        cache_types = [_LABEL_CACHE_TYPE.get(lbl, "multiplicity") for lbl in wanted]
    else:
        # Legacy single-binary mode: auto-dispatch could pick any flat-family cache —
        # be conservative and assume the hungriest (multiplicity), plus lru/hash-only.
        cache_types = ["multiplicity", "lru", "hash-only"]
    capacity = args.cache_capacity or DEFAULT_CAPACITY_ENTRIES

    limit, mem_source = effective_memory_limit()
    worker_bytes = planning_worker_bytes(cache_types, capacity)

    if limit <= 0:
        import multiprocessing
        default_workers = max(1, multiprocessing.cpu_count() // 2)
        print(f"WARNING: could not determine memory limit; defaulting to {default_workers} "
              f"workers. Use --workers to override.", file=sys.stderr)
        effective_requested = args.workers if args.workers is not None else default_workers
        jobs_result = JobsResult(jobs=effective_requested, max_safe=effective_requested,
                                 limiting_factor="unknown memory", warnings=[])
    else:
        max_safe = compute_jobs(10 ** 9, limit, worker_bytes).max_safe
        effective_requested = args.workers if args.workers is not None else max_safe
        jobs_result = compute_jobs(effective_requested, limit, worker_bytes)
        print(f"[orchestrator] memory limit {limit / 2**30:.0f} GB ({mem_source}); "
              f"~{worker_bytes / 2**30:.2f} GB/worker "
              f"({','.join(sorted(set(cache_types)))}); max_safe={max_safe}; "
              f"using {jobs_result.jobs} workers", file=sys.stderr)

    for w in jobs_result.warnings:
        print(w, file=sys.stderr)

    num_workers = jobs_result.jobs

    # ---------------------------------------------------------------------------
    # Game and solver configs
    # ---------------------------------------------------------------------------
    if args.full:
        game_configs = GAME_CONFIGS_FULL
        scope_label = "FULL (14 games, 500 seeds, 20-min timeout each)"
    else:
        game_configs = GAME_CONFIGS_QUICK
        scope_label = "QUICK (5 games, 50 seeds, 30 s timeout each) — use --full for full matrix"

    # Apply --seeds override if given
    seed_lo = seed_hi = None
    if args.seeds:
        parts = args.seeds.split("-")
        try:
            seed_lo, seed_hi = int(parts[0]), int(parts[1])
        except (IndexError, ValueError):
            print(f"Error: --seeds must be N-M (e.g. 1-100), got '{args.seeds}'", file=sys.stderr)
            sys.exit(1)
        game_configs = [
            (g, (seed_lo, seed_hi), t, s, n) for (g, _, t, s, n) in game_configs
        ]

    # Apply --timeout override if given
    if args.timeout is not None:
        game_configs = [
            (g, seeds, args.timeout, s, n) for (g, seeds, _, s, n) in game_configs
        ]

    if args.games:
        game_configs = [g for g in game_configs if g[0] in args.games]
        if not game_configs:
            # Game not in current scope; synthesize a minimal entry.
            # Use the --seeds override if given, else a small default.
            fb_seeds = (seed_lo, seed_hi) if args.seeds else (1, 50)
            fb_timeout = args.timeout if args.timeout is not None else 60000
            game_configs = [(g, fb_seeds, fb_timeout, "auto-foundations", "") for g in args.games]

    # ---------------------------------------------------------------------------
    # Resolve solver configurations (dry-run: skip binary checks)
    # ---------------------------------------------------------------------------
    bench_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "run_benchmark.py")
    if not dry_run and not os.path.exists(bench_script):
        print(f"Error: {bench_script} not found.", file=sys.stderr)
        sys.exit(1)

    if args.solver_dir:
        solver_dir = os.path.abspath(args.solver_dir)
        active_configs = []
        wanted = set(args.solvers) if args.solvers else None
        for label, binary_name, extra_args, skip_ineligible in SOLVER_CONFIGS:
            if wanted and label not in wanted:
                continue
            binary_path = os.path.join(solver_dir, binary_name)
            if dry_run or os.path.isfile(binary_path):
                active_configs.append((label, binary_path, extra_args, skip_ineligible))
            else:
                print(f"[warn] {binary_name} not found in {solver_dir} — skipping '{label}'",
                      file=sys.stderr)
        if not dry_run and not active_configs:
            print(f"Error: no solver binaries found in {solver_dir}", file=sys.stderr)
            sys.exit(1)
        if not active_configs:
            # dry-run with no solver-dir specified: synthesize a placeholder
            active_configs = [(lbl, f"<{bn}>", ea, si)
                               for lbl, bn, ea, si in SOLVER_CONFIGS
                               if not wanted or lbl in wanted]
        print(f"Multi-binary mode: {[label for label, *_ in active_configs]}")
    elif args.solver:
        active_configs = [
            (name, args.solver, extra_args, skip_ineligible)
            for name, extra_args, skip_ineligible in LEGACY_CACHE_CONFIGS
            if name in args.configs
        ]
        print(f"Single-binary mode: {args.solver}")
    else:
        # dry-run with neither --solver nor --solver-dir
        active_configs = [("default", "<solver>", [], False)]

    chunk_size = 5 if not args.full else 10

    # ---------------------------------------------------------------------------
    # Build task list
    # ---------------------------------------------------------------------------
    tasks = []  # (cmd_list, chunk_csv, chunk_json, label_str, timeout_s)

    output_dir = args.output_dir

    for game_type, (seed_lo, seed_hi), timeout_ms, streamliner, _ in game_configs:
        for label, solver_path, solver_args, skip_ineligible in active_configs:
            for c_start, c_end in chunk_range(seed_lo, seed_hi, chunk_size):
                chunk_base = os.path.join(
                    output_dir,
                    f"chunk_{game_type}_{label}_{c_start}_{c_end}",
                )
                chunk_csv = f"{chunk_base}.csv"
                chunk_json = f"{chunk_base}.json"

                cmd = [
                    sys.executable, bench_script,
                    "--solver", solver_path,
                    "--type", game_type,
                    "--seeds", f"{c_start}-{c_end}",
                    "--output", chunk_csv,
                    "--output-json", chunk_json,
                    "--no-summary",
                    "--label", label,
                ]
                if streamliner and streamliner != "none":
                    cmd.extend(["--streamliner", streamliner])
                if timeout_ms:
                    cmd.extend(["--timeout", str(timeout_ms)])
                if args.cache_capacity:
                    cmd.extend(["--cache-capacity", str(args.cache_capacity)])
                if skip_ineligible:
                    cmd.append("--skip-ineligible")
                if solver_args:
                    cmd.append("--")
                    cmd.extend(solver_args)

                seeds_in_chunk = c_end - c_start + 1
                if args.chunk_timeout is not None:
                    ceiling_s = float(args.chunk_timeout)
                else:
                    ceiling_s = chunk_ceiling_s(seeds_in_chunk, timeout_ms)

                chunk_label = f"{game_type} seeds {c_start}-{c_end} [{label}]"
                tasks.append((cmd, chunk_csv, chunk_json, chunk_label, ceiling_s))

    total = len(tasks)

    # ---------------------------------------------------------------------------
    # Print plan (always, and exit early on --dry-run)
    # ---------------------------------------------------------------------------
    print(f"")
    print(f"Scope:          {scope_label}")
    print(f"Total chunks:   {total}")
    print(f"Workers:        {num_workers}  (limiting factor: {jobs_result.limiting_factor})")
    if limit > 0:
        print(f"Memory:         limit {limit / 2**30:.0f} GB ({mem_source}); "
              f"~{worker_bytes / 2**30:.2f} GB/worker; max_safe={jobs_result.max_safe}")
    print(f"Engine:         GNU parallel (--jobs {num_workers})")
    print(f"Output dir:     {output_dir}")
    if args.cache_capacity:
        print(f"Cache capacity: {args.cache_capacity:,} entries")
    print()

    if dry_run:
        print(f"=== DRY RUN — {total} chunks planned ({num_workers} workers) ===")
        print()
        print("Commands that would be run (one per chunk):")
        print()
        for cmd, chunk_csv, chunk_json, chunk_label, ceiling_s in tasks:
            print(f"  # {chunk_label}  [ceiling={ceiling_s:.0f}s]")
            print(f"  {' '.join(cmd)}")
            print()
        print(f"(dry run — nothing executed)")
        return

    # ---------------------------------------------------------------------------
    # Create output dir and run via GNU parallel (T3)
    # ---------------------------------------------------------------------------
    os.makedirs(output_dir, exist_ok=True)

    print(f"[{datetime.now():%H:%M:%S}] Starting {total} chunks on {num_workers} workers "
          f"via GNU parallel")

    start_time = time.time()
    csv_chunks = []
    failed = 0

    # Write each chunk command to a script file, then drive them via parallel.
    # We use --joblog for progress visibility. We do NOT use --memfree: it would
    # KILL+requeue the youngest job under memory pressure (causing lost-work
    # "KILLED" runs); the accurate per-worker --jobs ceiling above is the defence.
    with tempfile.TemporaryDirectory() as tmpdir:
        # Write one script per chunk
        script_paths = []
        for i, (cmd, chunk_csv, chunk_json, chunk_label, ceiling_s) in enumerate(tasks):
            script_path = os.path.join(tmpdir, f"chunk_{i:05d}.sh")
            # Each chunk script runs run_chunk logic inline via Python so we
            # can reuse bench_lib.process kill discipline.  We pass the
            # ceiling as an env var to keep the invocation simple.
            with open(script_path, "w") as f:
                f.write("#!/bin/sh\n")
                # Each chunk is a Python one-liner that delegates back to
                # run_with_deadline.  We write a tiny helper script.
                f.write(
                    f"{sys.executable} -c \""
                    f"import sys, os; "
                    f"sys.path.insert(0, {repr(os.path.dirname(os.path.abspath(__file__)))}); "
                    f"from bench_lib.process import run_with_deadline; "
                    f"import subprocess, sys; "
                    f"r = run_with_deadline("
                    f"  {cmd!r}, "
                    f"  solver_timeout_s={ceiling_s!r}, "
                    f"  sigterm_grace_s=30.0"
                    f"); "
                    f"print(r.stdout, end='', file=sys.stdout); "
                    f"print(r.stderr, end='', file=sys.stderr); "
                    f"sys.exit(0 if r.returncode == 0 else 1)"
                    f"\"\n"
                )
            os.chmod(script_path, 0o755)
            script_paths.append((script_path, chunk_csv, chunk_json, chunk_label, ceiling_s))

        # Build the parallel job list (one script path per line)
        joblist_path = os.path.join(tmpdir, "joblist.txt")
        with open(joblist_path, "w") as f:
            for script_path, *_ in script_paths:
                f.write(script_path + "\n")

        joblog_path = os.path.join(tmpdir, "joblog.txt")

        parallel_cmd = [
            parallel_bin,
            "--jobs", str(num_workers),
            "--joblog", joblog_path,
            "--halt", "never",       # don't stop on individual failures
            "bash", ":::",
        ]
        # Add all script paths
        for sp, *_ in script_paths:
            parallel_cmd.append(sp)

        parallel_proc = subprocess.run(parallel_cmd, capture_output=False)

        # Parse joblog to classify successes/failures
        job_exit = {}
        if os.path.exists(joblog_path):
            with open(joblog_path) as f:
                for line in f:
                    if line.startswith("Seq"):
                        continue
                    parts = line.strip().split("\t")
                    if len(parts) >= 7:
                        try:
                            seq = int(parts[0])
                            exitval = int(parts[6])
                            job_exit[seq] = exitval
                        except (ValueError, IndexError):
                            pass

    # Determine which chunks succeeded based on whether their CSV exists
    for i, (cmd, chunk_csv, chunk_json, chunk_label, ceiling_s) in enumerate(tasks):
        seq = i + 1
        exitval = job_exit.get(seq, -1)
        if exitval == 0 and os.path.exists(chunk_csv):
            csv_chunks.append(chunk_csv)
        else:
            failed += 1
            if exitval != 0:
                print(f"[FAILED] Chunk {chunk_label} (exit {exitval})", file=sys.stderr)

    elapsed = time.time() - start_time
    print(f"[{datetime.now():%H:%M:%S}] All chunks done in {elapsed/60:.1f} minutes "
          f"({total - failed}/{total} succeeded, {failed} failed)")

    # ---------------------------------------------------------------------------
    # Merge
    # ---------------------------------------------------------------------------
    print("Merging chunked results...")
    combined_csv = os.path.join(output_dir, "combined.csv")
    merge_csvs(combined_csv, csv_chunks)

    print(f"\n[{datetime.now():%H:%M:%S}] Done in {elapsed/60:.1f} minutes")
    print(f"Results available at: {combined_csv}")
    if failed:
        print(f"WARNING: {failed} chunk(s) failed — their data is NOT in combined.csv.",
              file=sys.stderr)


if __name__ == "__main__":
    main()
