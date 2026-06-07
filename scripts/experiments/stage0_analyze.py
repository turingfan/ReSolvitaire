#!/usr/bin/env python3
"""Summarise Stage 0 depth-sweep CSVs for the depth-collapse go/no-go.

Reads one or more CSVs produced by stage0_depth_sweep.py and prints, per game:
  - outcome mix
  - depth stats for RESOLVED instances (winnable solution length / unwinnable
    proof depth) vs the depth reached by TIMEOUT instances
  - the "snake fraction": timeouts in pure monotonic descent (final_depth ==
    max_depth), i.e. the JAIR s5.1 tall-thin-tree pathology

Usage: stage0_analyze.py FILE.csv [FILE.csv ...]
"""
import csv
import sys


def pct(xs, p):
    if not xs:
        return None
    xs = sorted(xs)
    k = (len(xs) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(xs) - 1)
    return int(xs[lo] + (xs[hi] - xs[lo]) * (k - lo))


def to_int(v):
    try:
        return int(v)
    except (TypeError, ValueError):
        return None


def load(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def summarise(rows):
    by_outcome = {}
    for r in rows:
        by_outcome.setdefault(r["outcome"], []).append(r)
    return by_outcome


def depths(rows, field):
    return [d for d in (to_int(r.get(field)) for r in rows) if d is not None]


def main():
    files = sys.argv[1:]
    if not files:
        print(__doc__)
        sys.exit(1)

    for path in files:
        rows = load(path)
        game = rows[0]["game"] if rows else path
        n = len(rows)
        bo = summarise(rows)
        win = bo.get("winnable", [])
        uns = bo.get("unsolvable", [])
        tmo = bo.get("timeout", [])
        resolved = win + uns

        print(f"\n### {game}  (n={n})")
        mix = ", ".join(f"{k}={len(v)}" for k, v in sorted(bo.items()))
        print(f"- outcome mix: {mix}")

        if win:
            sd = depths(win, "final_depth")
            print(f"- winnable solution depth (final_depth): "
                  f"min={min(sd)} median={pct(sd,50)} p90={pct(sd,90)} max={max(sd)}")
        if uns:
            pd = depths(uns, "max_depth")
            print(f"- unwinnable proof depth (max_depth):     "
                  f"min={min(pd)} median={pct(pd,50)} p90={pct(pd,90)} max={max(pd)}")
        if resolved:
            rd = depths(resolved, "max_depth")
            print(f"- RESOLVED max_depth overall:             "
                  f"min={min(rd)} median={pct(rd,50)} p90={pct(rd,90)} max={max(rd)}")
        if tmo:
            td = depths(tmo, "max_depth")
            snake = sum(1 for r in tmo
                        if to_int(r.get("final_depth")) is not None
                        and to_int(r.get("final_depth")) == to_int(r.get("max_depth")))
            print(f"- TIMEOUT max_depth reached (6s):         "
                  f"min={min(td)} median={pct(td,50)} p90={pct(td,90)} max={max(td)}")
            print(f"- TIMEOUT pure-descent snake fraction:    "
                  f"{snake}/{len(tmo)} (final_depth==max_depth)")

        # collapse signal: ratio of timeout depth to resolved depth
        if resolved and tmo:
            rd_med = pct(depths(resolved, "max_depth"), 50)
            td_med = pct(depths(tmo, "max_depth"), 50)
            if rd_med:
                print(f"- collapse signal: median timeout depth / median resolved "
                      f"depth = {td_med}/{rd_med} = {td_med/rd_med:.0f}x")


if __name__ == "__main__":
    main()
