#!/usr/bin/env python3
"""
Read a solver oracle JSON and emit one run_benchmark.py command per matching entry.

The first command includes the CSV header; subsequent commands use --no-header --append
so all runs accumulate into a single output file.

Usage:
    python3 scripts/oracle_to_benchmark_cmds.py [options] | bash

Example:
    python3 scripts/oracle_to_benchmark_cmds.py \\
        --oracle tests/oracles/level5.json \\
        --solution-type unsolvable \\
        --solver cmake-build-release/bin/solvitaire-flat \\
        --warmup 1 --iterations 3 --timeout 1800000 \\
        --skip-ineligible \\
        --output results/level5_flat.csv \\
        -- --force-lru
"""

import argparse
import json
import os
import re
import shlex
import sys


def extract_seed(instance_path: str) -> str:
    """Extract numeric seed from an oracle instance path like 'resources/level5/game_12345_unwinnable.json'."""
    m = re.search(r'_(\d+)_', instance_path)
    if not m:
        raise ValueError(f"Cannot extract seed from instance path: {instance_path!r}")
    return m.group(1)


def main():
    parser = argparse.ArgumentParser(
        description="Emit run_benchmark.py commands from an oracle JSON file"
    )
    parser.add_argument("--oracle", required=True, help="Path to oracle JSON file")
    parser.add_argument(
        "--solution-type",
        choices=["all", "solved", "unsolvable"],
        default="all",
        help="Filter entries by solution_type (default: all)",
    )
    parser.add_argument("--solver", required=True, help="Path to solver binary")
    parser.add_argument("--warmup", type=int, default=0, help="Warmup runs per instance")
    parser.add_argument("--iterations", type=int, default=1, help="Timed runs per instance")
    parser.add_argument("--timeout", type=int, default=60000, help="Timeout per run in ms")
    parser.add_argument("--output", required=True, help="Output CSV file for all commands")
    parser.add_argument("--label", default="", help="Label column value for all runs")
    parser.add_argument("--legacy", action="store_true",
                        help="Pass --legacy to each run_benchmark.py call (for legacy --classify output)")
    parser.add_argument("--skip-ineligible", action="store_true",
                        help="Pass --skip-ineligible to each run_benchmark.py call")
    parser.add_argument("--no-summary", action="store_true",
                        help="Pass --no-summary to each run_benchmark.py call")
    parser.add_argument("--script", default="scripts/run_benchmark.py",
                        help="Path to run_benchmark.py (default: scripts/run_benchmark.py)")
    parser.add_argument("solver_args", nargs=argparse.REMAINDER,
                        help="Extra args passed through to the solver (after --)")

    args = parser.parse_args()

    with open(args.oracle) as f:
        entries = json.load(f)

    if args.solution_type != "all":
        entries = [e for e in entries if e.get("solution_type") == args.solution_type]

    if not entries:
        print(f"# No entries matched solution_type={args.solution_type!r}", file=sys.stderr)
        sys.exit(0)

    # Build the passthrough solver args string (strip leading '--' separator if present)
    solver_passthrough = args.solver_args
    if solver_passthrough and solver_passthrough[0] == "--":
        solver_passthrough = solver_passthrough[1:]

    for i, entry in enumerate(entries):
        seed = extract_seed(entry["instance"])
        custom_rules = entry["custom_rules"]
        streamliner = entry.get("streamliner", "none")

        cmd = [
            "python3", args.script,
            "--solver", args.solver,
            "--seeds", seed,
            "--custom-rules", custom_rules,
            "--timeout", str(args.timeout),
            "--warmup", str(args.warmup),
            "--iterations", str(args.iterations),
            "--output", args.output,
        ]

        if streamliner != "none":
            cmd += ["--streamliner", streamliner]

        if args.label:
            cmd += ["--label", args.label]

        if args.legacy:
            cmd.append("--legacy")

        if args.skip_ineligible:
            cmd.append("--skip-ineligible")

        if args.no_summary:
            cmd.append("--no-summary")

        # All runs after the first append to the CSV without repeating the header
        if i > 0:
            cmd += ["--no-header", "--append"]

        if solver_passthrough:
            cmd += ["--"] + solver_passthrough

        print(shlex.join(cmd))


if __name__ == "__main__":
    main()
