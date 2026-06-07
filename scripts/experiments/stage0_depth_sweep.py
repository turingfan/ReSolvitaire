#!/usr/bin/env python3
"""Stage 0 depth-distribution sweep for the depth-bounded-search investigation.

Runs the solver across a range of --random seeds for one game type, parses the
--json output, and appends one CSV row per instance with the metrics that matter
for the depth-collapse go/no-go (proposal s1.3 / implementation-plan Stage 0):

    game,seed,outcome,final_depth,max_depth,states_searched,unique_states,
    backtracks,rss_bytes,wall_ms

The solver's own --timeout makes it self-terminate and still emit JSON (outcome
"timeout" with the max_depth reached so far), so deep/hard instances are captured
rather than lost. Measurement only -- no algorithm change.

Usage:
    stage0_depth_sweep.py --solver PATH --type GAME --seeds 1-50 \
        --timeout-ms 6000 --out results.csv
"""
import argparse
import csv
import json
import subprocess
import sys
import time

FIELDS = ["game", "seed", "outcome", "final_depth", "max_depth",
          "states_searched", "unique_states", "backtracks", "rss_bytes",
          "wall_ms"]


def parse_seeds(spec):
    out = []
    for part in spec.split(","):
        part = part.strip()
        if "-" in part:
            a, b = part.split("-")
            out.extend(range(int(a), int(b) + 1))
        elif part:
            out.append(int(part))
    return out


def run_one(solver, game, seed, timeout_ms):
    cmd = [solver, "--type", game, "--random", str(seed),
           "--json", "--timeout", str(timeout_ms)]
    wall_budget = timeout_ms / 1000.0 + 60  # generous margin over solver timeout
    t0 = time.time()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True,
                           timeout=wall_budget)
    except subprocess.TimeoutExpired:
        return {"game": game, "seed": seed, "outcome": "WALL_TIMEOUT",
                "wall_ms": int((time.time() - t0) * 1000)}
    wall_ms = int((time.time() - t0) * 1000)
    try:
        d = json.loads(p.stdout.strip())
    except Exception:
        return {"game": game, "seed": seed, "outcome": "PARSE_FAIL",
                "wall_ms": wall_ms}
    return {"game": game, "seed": seed,
            "outcome": d.get("solution_type"),
            "final_depth": d.get("final_depth"),
            "max_depth": d.get("max_depth"),
            "states_searched": d.get("states_searched"),
            "unique_states": d.get("unique_states"),
            "backtracks": d.get("backtracks"),
            "rss_bytes": d.get("solver_resident_bytes"),
            "wall_ms": wall_ms}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--solver", required=True)
    ap.add_argument("--type", required=True, dest="game")
    ap.add_argument("--seeds", required=True, help="e.g. 1-50 or 1,2,3 or 1-40,77")
    ap.add_argument("--timeout-ms", type=int, default=6000)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    seeds = parse_seeds(args.seeds)
    with open(args.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        for s in seeds:
            row = run_one(args.solver, args.game, s, args.timeout_ms)
            w.writerow(row)
            f.flush()
            print(f"{args.game} seed={s} {row.get('outcome')} "
                  f"max_depth={row.get('max_depth')} "
                  f"final_depth={row.get('final_depth')}", file=sys.stderr)
    print(f"done: {args.game} -> {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
