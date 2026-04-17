#!/usr/bin/env python3
"""
Benchmark runner for ReSolvitaire.

Orchestrates solver runs across multiple seeds or instances, captures timing
and memory statistics, and optionally generates R summary statistics.

Usage:
    python3 run_benchmark.py --solver PATH --seeds N-M --type TYPE --output results.csv
    python3 run_benchmark.py --solver PATH --instances '*.json' --output results.csv


"""

import argparse
import functools
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

_CSV_LINE_RE = re.compile(r'^\d+\s*,')

def get_solver_commit() -> str:
    """Get short commit hash of the solver repository."""
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, cwd=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        )
        return result.stdout.strip() if result.returncode == 0 else "unknown"
    except Exception:
        return "unknown"

def parse_seed_range(seed_spec: str) -> List[int]:
    """Parse seed specification like '1-10' into [1, 2, ..., 10]."""
    parts = seed_spec.split('-')
    if len(parts) != 2:
        raise ValueError(f"Invalid seed range: {seed_spec}")
    try:
        start, end = int(parts[0]), int(parts[1])
        return list(range(start, end + 1))
    except ValueError:
        raise ValueError(f"Invalid seed range: {seed_spec}")

def glob_expand(patterns: List[str]) -> List[str]:
    """Expand glob patterns into sorted list of file paths."""
    files = []
    for pattern in patterns:
        paths = list(Path(".").glob(pattern))
        files.extend([str(p) for p in paths])
    return sorted(files)

def parse_rss_from_time_output(stderr: str) -> int:
    """
    Extract peak RSS in bytes from /usr/bin/time stderr output.

    macOS (-l flag):   '  3202760704  maximum resident set size'  → bytes
    Linux  (-v flag):  'Maximum resident set size (kbytes): 3127296' → KB * 1024
    """
    # macOS format
    m = re.search(r'(\d+)\s+maximum resident set size', stderr)
    if m:
        return int(m.group(1))
    # Linux format
    m = re.search(r'Maximum resident set size \(kbytes\): (\d+)', stderr)
    if m:
        return int(m.group(1)) * 1024
    return 0


@functools.lru_cache(maxsize=None)
def time_prefix() -> List[str]:
    """
    Return the /usr/bin/time prefix appropriate for this platform, or [] if unavailable.

    macOS: /usr/bin/time -l   (BSD time, outputs to stderr, RSS in bytes)
    Linux: /usr/bin/time -v   (GNU time, outputs to stderr, RSS in KB)

    Result is cached — platform and binary presence don't change mid-run.
    """
    time_bin = "/usr/bin/time"
    if not os.path.isfile(time_bin):
        return []
    if platform.system() == "Darwin":
        return [time_bin, "-l"]
    else:
        return [time_bin, "-v"]


def run_solver(cmd: List[str], timeout_ms: int) -> Tuple[bool, str, str, float, int]:
    """
    Run solver subprocess, measure wall-clock time and peak RSS.
    Returns (success, stdout, stderr, time_us, rss_bytes).

    Wraps the command with /usr/bin/time (platform-appropriate) to get
    per-run peak RSS from stderr. Falls back to rss_bytes=0 if unavailable.

    The solver's own --timeout flag is the primary time enforcer.
    Python's safety valve fires at 3× that limit for truly hung processes:
      1. Send SIGTERM, wait up to 5 s for the process to exit and emit output.
      2. If still alive, send SIGKILL.
    Whatever stdout is available after SIGTERM is returned so the caller can
    write a partial row rather than losing the run entirely.
    """
    import signal

    safety_timeout_s = timeout_ms / 1000.0 * 3
    prefix = time_prefix()
    full_cmd = prefix + cmd

    t0 = time.perf_counter()
    try:
        proc = subprocess.Popen(
            full_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            stdout, stderr = proc.communicate(timeout=safety_timeout_s)
            t1 = time.perf_counter()
            time_us = (t1 - t0) * 1_000_000
            rss_bytes = parse_rss_from_time_output(stderr) if prefix else 0
            return proc.returncode == 0, stdout, stderr, time_us, rss_bytes

        except subprocess.TimeoutExpired:
            try:
                proc.send_signal(signal.SIGTERM)
            except OSError:
                pass
            try:
                stdout, stderr = proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    proc.kill()
                except OSError:
                    pass
                stdout, stderr = proc.communicate()

            t1 = time.perf_counter()
            time_us = (t1 - t0) * 1_000_000
            rss_bytes = parse_rss_from_time_output(stderr) if prefix else 0
            return False, stdout, stderr, time_us, rss_bytes

    except Exception:
        t1 = time.perf_counter()
        time_us = (t1 - t0) * 1_000_000
        return False, "", "", time_us, 0

def parse_legacy_classify(text: str) -> Dict:
    """
    Parse --classify CSV output from either legacy or current solver.

    Non-smart (13 columns):
      seed, sol_type, time_ms, states, unique, backtracks, dom_moves,
      removed, cache_size, cache_buckets, max_depth, depth, final_sol

    Smart-solvability (24 columns, always — second pass may be empty):
      seed, sol1_type, time1, states1, unique1, backtracks1, dom1,
      removed1, cache_size1, cache_buckets1, max_depth1, depth1,
      [sol1_type_repeat OR empty], [10 second-pass stats OR 10 empty],
      final_sol

    Strategy: always use the final column for solution_type.
    For stats: use second-pass columns if second pass ran (col 12 non-empty),
    else use first-pass columns.
    """
    defaults = {
        "solution_type": "UNKNOWN",
        "states_searched": 0,
        "unique_states": 0,
        "backtracks": 0,
        "max_depth": 0,
        "dominance_moves": 0,
        "states_removed_from_cache": 0,
        "cache_size": 0,
        "cache_buckets": 0,
        "final_depth": 0,
        "solver_resident_bytes": 0,
    }

    def map_sol_type(s: str) -> str:
        s = s.strip().lower()
        if s == "solved":
            return "SOLVED"
        if s == "timed-out":
            return "TIMEOUT"
        if "unsolvable" in s:
            return "UNWINNABLE"
        return "UNKNOWN"

    def safe_int(s: str) -> int:
        try:
            return int(s.strip())
        except (ValueError, AttributeError):
            return 0

    try:
        # Find the first CSV line with content (skip any noise before it)
        csv_line = ""
        for line in text.splitlines():
            stripped = line.strip()
            if stripped and _CSV_LINE_RE.match(stripped):
                csv_line = stripped
                break
        if not csv_line:
            return defaults

        parts = [p.strip() for p in csv_line.split(',')]
        n = len(parts)

        result = dict(defaults)

        def assign_stats(base: int) -> None:
            keys = ["states_searched", "unique_states", "backtracks", "dominance_moves",
                    "states_removed_from_cache", "cache_size", "cache_buckets", "max_depth", "final_depth"]
            for i, key in enumerate(keys):
                result[key] = safe_int(parts[base + i])

        if n == 13:
            # Non-smart: straightforward
            result["solution_type"] = map_sol_type(parts[12])
            assign_stats(3)

        elif n == 24:
            # Smart-solvability: col 23 is always the final result
            result["solution_type"] = map_sol_type(parts[23])
            # Use second-pass stats if second pass ran (col 12 non-empty)
            if parts[12].strip():
                assign_stats(14)  # Second pass ran: cols 14-22
            else:
                assign_stats(3)   # No second pass: use first-pass stats, cols 3-11

        else:
            # Unknown format — return defaults with whatever solution type we can find
            result["solution_type"] = map_sol_type(parts[-1]) if parts else "UNKNOWN"

        return result

    except Exception:
        return defaults


def parse_solver_json(json_str: str) -> Dict:
    """Parse solver JSON output, with defaults for missing fields."""
    defaults = {
        "solution_type": "UNKNOWN",
        "states_searched": 0,
        "unique_states": 0,
        "backtracks": 0,
        "max_depth": 0,
        "dominance_moves": 0,
        "states_removed_from_cache": 0,
        "cache_size": 0,
        "cache_buckets": 0,
        "final_depth": 0,
        "solver_resident_bytes": 0,
    }
    try:
        data = json.loads(json_str)
        # Map solution_type values
        sol_type = data.get("solution_type", "UNKNOWN")
        if sol_type == "winnable":
            sol_type = "SOLVED"
        elif sol_type == "unsolvable":
            sol_type = "UNWINNABLE"
        elif sol_type == "timeout":
            sol_type = "TIMEOUT"
        else:
            sol_type = "UNKNOWN"
        data["solution_type"] = sol_type
        # Merge with defaults
        for key, val in defaults.items():
            if key not in data:
                data[key] = val
        return data
    except Exception:
        defaults["solution_type"] = "UNKNOWN"
        return defaults

def format_progress(index: int, total: int, instance: str, solution_type: str, time_us: float, nodes: int) -> str:
    """Format a progress line."""
    return f"[{index:3d}/{total:3d}] {instance:20s} {solution_type:12s} {time_us:12.1f} us {nodes:8d} nodes"

def main():
    parser = argparse.ArgumentParser(
        description="Benchmark ReSolvitaire across multiple instances"
    )
    parser.add_argument("--solver", required=True, help="Path to solvitaire binary")
    parser.add_argument("--type", help="Game type (required for --seeds mode)")
    parser.add_argument("--seeds", help="Seed range N-M (inclusive)")
    parser.add_argument("--instances", nargs="+", help="Glob patterns for instance files")
    parser.add_argument("--legacy", action="store_true", help="Use --classify flag for legacy Solvitaire solver")
    parser.add_argument("--streamliner", default="none",
                        choices=["none", "auto-foundations", "suit-symmetry", "both", "smart-solvability"],
                        help="Streamliner mode")
    parser.add_argument("--cache-capacity", type=int, default=None, help="Cache capacity in bytes")
    parser.add_argument("--timeout", type=int, default=60000, help="Timeout per instance in ms")
    parser.add_argument("--iterations", type=int, default=1, help="Number of timed runs per instance")
    parser.add_argument("--warmup", type=int, default=0, help="Number of warmup runs (excluded from output)")
    parser.add_argument("--output", required=True, help="Output CSV file")
    parser.add_argument("--output-json", help="Optional JSON output file")
    parser.add_argument("--no-header", action="store_true", help="Don't write CSV header")
    parser.add_argument("--no-summary", action="store_true", help="Skip automatic R summary")
    parser.add_argument("--skip-ineligible", action="store_true",
                        help="If solver rejects the game type (non-zero exit with 'requires'/"
                             "'not eligible'/'not supported' in stderr), skip remaining seeds "
                             "and exit cleanly. Use for variant binaries.")
    parser.add_argument("--label", default="", help="Label for this benchmark run (e.g., cache config)")
    parser.add_argument("solver_args", nargs=argparse.REMAINDER, help="Additional arguments to pass to the solver")

    args = parser.parse_args()

    # Validate inputs
    if not args.seeds and not args.instances:
        print("Error: either --seeds or --instances is required", file=sys.stderr)
        sys.exit(1)
    if args.seeds and not args.type:
        print("Error: --type is required when using --seeds", file=sys.stderr)
        sys.exit(1)

    # Determine mode and build instance list
    seed_mode = bool(args.seeds)
    if seed_mode:
        seeds = parse_seed_range(args.seeds)
        instances = [f"{args.type}_{seed}" for seed in seeds]
    else:
        instance_files = glob_expand(args.instances)
        instances = [os.path.splitext(os.path.basename(f))[0] for f in instance_files]
        seeds = [None] * len(instance_files)

    # Capture solver commit once
    solver_commit = get_solver_commit()

    # Open CSV and write header
    csv_file = open(args.output, "w")
    if not args.no_header:
        header = "instance,seed,run,solution_type,time_us,nodes,unique_nodes,backtracks,dominance_moves,states_removed_from_cache,cache_size,cache_buckets,max_depth,final_depth,resident_memory_bytes,solver_resident_bytes,streamliner,cache_capacity,timeout_ms,solver_commit,label"
        csv_file.write(header + "\n")
        csv_file.flush()

    json_results = []
    total_instances = len(instances)
    overall_index = 1

    try:
        for instance_idx, (instance, seed) in enumerate(zip(instances, seeds)):
            # Build solver command
            if args.legacy:
                cmd = [args.solver, "--classify", "--timeout", str(args.timeout)]
            else:
                cmd = [args.solver, "--json", "--timeout", str(args.timeout)]

            if seed_mode:
                cmd += ["--type", args.type, "--random", str(seed)]
            else:
                cmd += [instance_files[instance_idx]]

            if args.streamliner != "none":
                cmd += ["--streamliners", args.streamliner]

            if args.cache_capacity is not None:
                cmd += ["--cache-capacity", str(args.cache_capacity)]

            if args.solver_args:
                # Remove the '--' separator if argparse left it as the first element
                trailing = args.solver_args[1:] if args.solver_args[0] == '--' else args.solver_args
                cmd += trailing

            # Run warmup runs
            for warmup_run in range(args.warmup):
                run_solver(cmd, args.timeout)

            # Run timed runs
            ineligible = False
            for run_num in range(1, args.iterations + 1):
                success, json_output, solver_stderr, time_us, rss_bytes = run_solver(cmd, args.timeout)

                # Check for ineligible game/solver combination before writing any row.
                if not success and args.skip_ineligible:
                    stderr_lower = solver_stderr.lower()
                    if any(kw in stderr_lower for kw in ("requires", "not eligible", "not supported")):
                        print(f"[skip] ineligible for this solver — skipping remaining seeds",
                              file=sys.stderr)
                        ineligible = True
                        break

                parse = parse_legacy_classify if args.legacy else parse_solver_json
                if success:
                    solver_data = parse(json_output)
                elif json_output.strip():
                    # Process was killed but emitted partial/complete output — try to parse it.
                    solver_data = parse(json_output)
                    if solver_data["solution_type"] == "UNKNOWN":
                        solver_data["solution_type"] = "TERMINATED"
                else:
                    solver_data = None

                if solver_data is not None:
                    solution_type = solver_data["solution_type"]
                    nodes = solver_data["states_searched"]
                    unique_nodes = solver_data["unique_states"]
                    backtracks = solver_data["backtracks"]
                    dominance_moves = solver_data["dominance_moves"]
                    states_removed = solver_data["states_removed_from_cache"]
                    cache_size = solver_data["cache_size"]
                    cache_buckets = solver_data["cache_buckets"]
                    max_depth = solver_data["max_depth"]
                    final_depth = solver_data["final_depth"]
                    solver_rss = solver_data["solver_resident_bytes"]
                else:
                    # No output at all — hard kill with no data.
                    solution_type = "KILLED"
                    nodes = unique_nodes = backtracks = dominance_moves = 0
                    states_removed = cache_size = cache_buckets = 0
                    max_depth = final_depth = solver_rss = 0

                # Write CSV row
                seed_str = str(seed) if seed is not None else ""
                csv_row = f"{instance},{seed_str},{run_num},{solution_type},{time_us:.0f},{nodes},{unique_nodes},{backtracks},{dominance_moves},{states_removed},{cache_size},{cache_buckets},{max_depth},{final_depth},{rss_bytes},{solver_rss},{args.streamliner},{args.cache_capacity if args.cache_capacity is not None else ''},{args.timeout},{solver_commit},{args.label}"
                csv_file.write(csv_row + "\n")
                csv_file.flush()

                # Build JSON record (same fields as CSV)
                json_record = {
                    "instance": instance,
                    "seed": seed_str,
                    "run": run_num,
                    "solution_type": solution_type,
                    "time_us": time_us,
                    "nodes": nodes,
                    "unique_nodes": unique_nodes,
                    "backtracks": backtracks,
                    "dominance_moves": dominance_moves,
                    "states_removed_from_cache": states_removed,
                    "cache_size": cache_size,
                    "cache_buckets": cache_buckets,
                    "max_depth": max_depth,
                    "final_depth": final_depth,
                    "resident_memory_bytes": rss_bytes,
                    "solver_resident_bytes": solver_rss,
                    "streamliner": args.streamliner,
                    "cache_capacity": args.cache_capacity,
                    "timeout_ms": args.timeout,
                    "solver_commit": solver_commit,
                    "label": args.label,
                }
                json_results.append(json_record)

                # Print progress (only for first run if multiple iterations)
                if run_num == 1:
                    progress_line = format_progress(overall_index, total_instances, instance, solution_type, time_us, nodes)
                    print(progress_line, file=sys.stderr)
                    overall_index += 1

            if ineligible:
                break  # skip remaining seeds for this game/solver combination

    finally:
        csv_file.close()

    # Write JSON output if requested
    if args.output_json:
        with open(args.output_json, "w") as f:
            json.dump(json_results, f, indent=2)

    # Run R summary unless --no-summary
    if not args.no_summary:
        rscript_path = shutil.which("Rscript")
        if rscript_path:
            try:
                subprocess.run(
                    ["Rscript", "analysis/summary.R", args.output],
                    check=False
                )
            except Exception:
                pass
        else:
            print("(R not available — skipping summary. Install R to enable.)", file=sys.stderr)

if __name__ == "__main__":
    main()
