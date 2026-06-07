#!/usr/bin/env python3
"""
Differential-verdict harness (Stage 1 item 1f).

Runs the solver in iterative-deepening mode (--initial-depth-bound L0, growing by
--depth-grow) over a set of oracle instances and asserts that the bounded FINAL
verdict equals the unbounded oracle's verdict for every instance:

    winnable  (oracle) <-> winnable  (bounded ID run)
    unsolvable(oracle) <-> unsolvable(bounded ID run)

Where the unbounded oracle is `timeout` there is no constraint (the oracle has no
ground truth for that instance). A bounded run that itself times out is likewise
not counted as a mismatch (it is "unknown", not a wrong verdict) — but for the
fast L1 set you should pick an L0 large enough that nothing times out, so every
instance contributes a real verdict-agreement check.

THE PROJECT RED LINE: a bounded `unsolvable` where the oracle is NOT unsolvable
(or a bounded `winnable` where the oracle is `unsolvable`) is a HARD FAILURE. This
script exits non-zero with a clear message on ANY such mismatch.

This is a thin wrapper around scripts/regression_runner.py (it does NOT reinvent
the comparison): it invokes the runner with the iterative-deepening flags and the
verdict-only comparison policy, then parses the runner's summary so it can print a
crisp "N/N verdicts match" line and propagate a loud non-zero exit on any
mismatch.

Usage (fast L1, default):
    python3 scripts/differential_verdict.py --exe cmake-build-release/bin/solvitaire

    python3 scripts/differential_verdict.py \
        --exe cmake-build-release/bin/solvitaire \
        --initial-depth-bound 1000 --depth-grow 2

L2 (seed-based, slower):
    python3 scripts/differential_verdict.py \
        --exe cmake-build-release/bin/solvitaire --level 2 \
        --initial-depth-bound 1000 --max-instance-timeout-ms 60000
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
RUNNER = os.path.join(HERE, "regression_runner.py")


def main():
    ap = argparse.ArgumentParser(
        description="Differential-verdict harness: bounded iterative-deepening "
                    "final verdict must equal the unbounded oracle verdict.")
    ap.add_argument("--exe", required=True, help="Path to solvitaire executable.")
    ap.add_argument("--level", type=int, default=1,
                    help="Regression level (default 1). Level 1 is JSON-based and "
                         "fast; 2-5 are seed-based and slower.")
    ap.add_argument("--oracle", default=None,
                    help="Override oracle path (default: tests/oracles/level<L>.json).")
    ap.add_argument("--instances", default=None,
                    help="Override instances dir (default for L1: "
                         "tests/resources/level1; omitted for L>=2).")
    ap.add_argument("--initial-depth-bound", type=int, default=1000,
                    help="Initial depth bound L0 (default 1000). Pick large enough "
                         "that shallow-unsolvable AND winnable instances both resolve "
                         "by deepening rather than timing out.")
    ap.add_argument("--depth-grow", type=int, default=2,
                    help="Depth-bound growth factor between passes (default 2).")
    ap.add_argument("--max-depth-bound", type=int, default=None,
                    help="Optional L_max cap (default: none — deepen until timeout).")
    ap.add_argument("--max-instance-timeout-ms", type=int, default=120000,
                    help="Per-instance solver timeout cap in ms (default 120000).")
    ap.add_argument("--verbose", action="store_true",
                    help="Pass --verbose to the runner (per-instance lines).")
    args = ap.parse_args()

    oracle = args.oracle or os.path.join(REPO, "tests", "oracles",
                                         f"level{args.level}.json")
    if args.instances is not None:
        instances = args.instances
    elif args.level == 1:
        instances = os.path.join(REPO, "tests", "resources", "level1")
    else:
        instances = ""  # seed-based levels: no instances dir

    cmd = [
        sys.executable, RUNNER,
        "--exe", args.exe,
        "--oracle", oracle,
        "--max-instance-timeout-ms", str(args.max_instance_timeout_ms),
        # Verdict-only: node counts legitimately differ under depth bounding, so we
        # do NOT enforce them. Outcome flips are still hard failures in the runner.
        "--compare-outcome-only",
        "--initial-depth-bound", str(args.initial_depth_bound),
        "--depth-grow", str(args.depth_grow),
    ]
    if instances:
        cmd.extend(["--instances", instances])
    if args.max_depth_bound is not None:
        cmd.extend(["--max-depth-bound", str(args.max_depth_bound)])
    if args.verbose:
        cmd.append("--verbose")

    print("=" * 70, flush=True)
    print("DIFFERENTIAL-VERDICT HARNESS (Stage 1 item 1f)", flush=True)
    print(f"  level={args.level} oracle={os.path.basename(oracle)} "
          f"L0={args.initial_depth_bound} grow={args.depth_grow} "
          f"max={args.max_depth_bound}", flush=True)
    print(f"  command: {' '.join(cmd)}", flush=True)
    print("=" * 70, flush=True)

    proc = subprocess.run(cmd, capture_output=True, text=True)
    out = proc.stdout
    sys.stdout.write(out)
    if proc.stderr:
        sys.stderr.write(proc.stderr)

    # Parse the runner's summary so we can emit a crisp verdict-match line.
    passed = total = failed = None
    m = re.search(r"Final Report: Passed: (\d+)/(\d+)", out)
    if m:
        passed, total = int(m.group(1)), int(m.group(2))
    m = re.search(r"FAILED: (\d+)/(\d+)", out)
    if m:
        failed = int(m.group(1))

    print("-" * 70, flush=True)
    if proc.returncode == 0 and passed is not None:
        print(f"DIFFERENTIAL VERDICT: PASS — {passed}/{total} verdicts match "
              f"(bounded iterative-deepening == unbounded oracle).", flush=True)
        return 0
    else:
        nfail = failed if failed is not None else "?"
        ntot = total if total is not None else "?"
        print(f"DIFFERENTIAL VERDICT: FAIL — {nfail}/{ntot} instance(s) DISAGREE "
              f"with the unbounded oracle. THIS IS THE PROJECT RED LINE: a bounded "
              f"verdict must never contradict the unbounded verdict. See [FAIL] "
              f"lines above.", flush=True)
        # Always non-zero on failure (propagate the runner's code, but never 0).
        return proc.returncode if proc.returncode != 0 else 1


if __name__ == "__main__":
    sys.exit(main())
