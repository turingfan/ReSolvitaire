#!/usr/bin/env python3
"""
Benchmark runner for ReSolvitaire.

Orchestrates solver runs across multiple seeds or instances, captures timing
and memory statistics, and optionally generates R summary statistics.

Usage:
    # Preset game type, seed range
    python3 run_benchmark.py --solver PATH --seeds N-M --type TYPE --output results.csv

    # Single seed with custom rules JSON
    python3 run_benchmark.py --solver PATH --seeds SEED --custom-rules RULES.json --output results.csv

    # File-based instances (Level 1 JSON deal files)
    python3 run_benchmark.py --solver PATH --instances '*.json' --output results.csv

--custom-rules / --type are mutually exclusive.  --seeds accepts either a
range (N-M) or a single integer.

Use --append with --no-header to accumulate results from successive calls
(e.g. one call per instance) into a single CSV without duplicate headers.
See scripts/oracle_to_benchmark_cmds.py for a helper that generates these
chained calls from an oracle JSON file.
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

# bench_lib is a sibling package under scripts/.  Add scripts/ to sys.path so
# it is importable whether run_benchmark.py is invoked directly or as a module.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench_lib.process import (
    EXITED_OK,
    run_with_deadline,
)

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
    """Parse seed specification like '1-10' or single '42' into a list of ints."""
    parts = seed_spec.split('-')
    if len(parts) == 1:
        try:
            return [int(parts[0])]
        except ValueError:
            raise ValueError(f"Invalid seed: {seed_spec}")
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
    per-run peak RSS from stderr. Falls back to 0 if the time RSS line is
    unavailable after a kill (e.g. /usr/bin/time never completed its own
    output). Never writes a wrong RSS value.

    Delegates process-group kill discipline to bench_lib.process.run_with_deadline:
    - The solver's own --timeout flag is the primary time enforcer.
    - Python deadline = 1.5 × solver_timeout_s (D1: no floor, no cap).
    - On overrun: SIGTERM the process group → wait sigterm_grace_s → SIGKILL.
    - The solver AND any /usr/bin/time wrapper share the process group
      (start_new_session=True) so a single killpg() reaches both.
    - Whatever stdout is available is always returned regardless of how the
      process ended.

    The returned `success` bool is True only when disposition is EXITED_OK
    (returncode 0 and within the deadline).  Callers use it to decide whether
    to trust the RSS from /usr/bin/time, but disposition is the authoritative
    classification signal for downstream CSV column assignment.
    """
    solver_timeout_s = timeout_ms / 1000.0
    sigterm_grace_s = 30.0   # fixed flush window after SIGTERM (D1)

    prefix = time_prefix()
    full_cmd = prefix + cmd

    result = run_with_deadline(
        full_cmd,
        solver_timeout_s=solver_timeout_s,
        sigterm_grace_s=sigterm_grace_s,
    )

    # Try to parse RSS from /usr/bin/time stderr.  On kill paths the time
    # wrapper may not have flushed its RSS line, so parse_rss_from_time_output
    # will return 0 (never invents a number).  The caller further falls back
    # to solver_resident_bytes from solver JSON if rss_bytes is 0.
    rss_bytes = parse_rss_from_time_output(result.stderr) if prefix else 0

    success = result.disposition == EXITED_OK
    return success, result.stdout, result.stderr, result.wall_us, rss_bytes

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
        # Map solution_type values from solver JSON to CSV vocabulary.
        # Every recognised solver outcome must map to a non-UNKNOWN value so
        # that downstream hooks (which do not count UNKNOWN rows) do not
        # silently lose data.
        sol_type = data.get("solution_type", "UNKNOWN")
        if sol_type == "winnable":
            sol_type = "SOLVED"
        elif sol_type == "unsolvable":
            sol_type = "UNWINNABLE"
        elif sol_type == "timeout":
            sol_type = "TIMEOUT"
        elif sol_type == "terminated":
            sol_type = "TERMINATED"
        elif sol_type == "failed":
            sol_type = "FAILED"
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
    # When invoked via a pipeline (e.g. oracle_to_benchmark_cmds.py | bash),
    # Python inherits a broken-pipe fd 0 that can cause solver subprocesses to
    # fail at fork time.  Replace it with /dev/null once at startup.
    try:
        _devnull = os.open(os.devnull, os.O_RDONLY)
        os.dup2(_devnull, 0)
        os.close(_devnull)
    except OSError:
        pass

    parser = argparse.ArgumentParser(
        description="Benchmark ReSolvitaire across multiple instances"
    )
    parser.add_argument("--solver", required=True, help="Path to solvitaire binary")
    parser.add_argument("--type", help="Game type (required for --seeds mode, mutually exclusive with --custom-rules)")
    parser.add_argument("--custom-rules", dest="custom_rules", help="Path to custom rules JSON (used with --seeds instead of --type)")
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
    parser.add_argument("--append", action="store_true", help="Append to output CSV instead of overwriting (use with --no-header for chained calls)")
    parser.add_argument("--no-summary", action="store_true", help="Skip automatic R summary")
    parser.add_argument("--skip-ineligible", action="store_true",
                        help="If solver rejects the game type (non-zero exit with 'requires'/"
                             "'not eligible'/'not supported' in stderr), skip remaining seeds "
                             "and exit cleanly. Use for variant binaries.")
    parser.add_argument("--label", default="", help="Label for this benchmark run (e.g., cache config)")
    parser.add_argument("solver_args", nargs=argparse.REMAINDER, help="Additional arguments to pass to the solver")

    args = parser.parse_args()
    args.solver = os.path.abspath(args.solver)

    # Validate inputs
    if not args.seeds and not args.instances:
        print("Error: either --seeds or --instances is required", file=sys.stderr)
        sys.exit(1)
    if args.seeds and not args.type and not args.custom_rules:
        print("Error: --type or --custom-rules is required when using --seeds", file=sys.stderr)
        sys.exit(1)
    if args.seeds and args.type and args.custom_rules:
        print("Error: --type and --custom-rules are mutually exclusive", file=sys.stderr)
        sys.exit(1)

    # Determine mode and build instance list
    seed_mode = bool(args.seeds)
    if seed_mode:
        seeds = parse_seed_range(args.seeds)
        if args.custom_rules:
            rules_name = os.path.splitext(os.path.basename(args.custom_rules))[0]
            instances = [f"{rules_name}_{seed}" for seed in seeds]
        else:
            instances = [f"{args.type}_{seed}" for seed in seeds]
    else:
        instance_files = glob_expand(args.instances)
        instances = [os.path.splitext(os.path.basename(f))[0] for f in instance_files]
        seeds = [None] * len(instance_files)

    # Capture solver commit once
    solver_commit = get_solver_commit()

    # Open CSV and write header
    csv_file = open(args.output, "a" if args.append else "w")
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
                if args.custom_rules:
                    cmd += ["--custom-rules", args.custom_rules, "--random", str(seed)]
                else:
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

                # Classification priority (from the plan):
                #   1. solver self-reported "timeout" in JSON → TIMEOUT (clean, has stats)
                #   2. wrapper had to kill but partial JSON parsed → TERMINATED
                #   3. no output at all → KILLED
                # Never write a bare UNKNOWN for a real outcome.
                if success:
                    # Clean exit: trust the solver's own solution_type field.
                    solver_data = parse(json_output)
                elif json_output.strip():
                    # Process was killed but emitted partial/complete output — try to parse.
                    solver_data = parse(json_output)
                    # If the parse returned a solver-reported type (SOLVED/UNWINNABLE/TIMEOUT/FAILED)
                    # keep it — the solver finished and reported cleanly before we fired.
                    # If the parsed type is still UNKNOWN (JSON incomplete/garbled), reclassify
                    # as TERMINATED to indicate partial-output-kill.
                    if solver_data["solution_type"] == "UNKNOWN":
                        solver_data["solution_type"] = "TERMINATED"
                else:
                    # No output at all: hard kill with no data.
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
                    # RSS fallback: if /usr/bin/time line was lost on kill path,
                    # use solver's own self-reported RSS rather than silently
                    # writing 0 when solver JSON has a value.
                    if rss_bytes == 0 and solver_rss > 0:
                        rss_bytes = solver_rss
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
