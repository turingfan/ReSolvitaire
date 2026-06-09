#!/usr/bin/env python3
"""M5 collapse measurement (RAM-safe). See docs/depth-bounded-search/m5-measurement-plan.md.

Per (instance x arm): run ONE solver child, capture its peak RSS (VmHWM via a /proc
monitor thread), parse the JSON verdict, and emit a CSV row. One run at a time; a monitor
thread hard-kills any child exceeding --ceiling-kib so a mis-estimate cannot OOM the box.

Arms (all share --force-lru + --cache-capacity + --timeout; only the schedule differs):
  T     : --initial-depth-bound L0 --depth-grow 2          (treatment: doubling ID + reuse + inf-collapse)
  B     : --initial-depth-bound 1000000                    (baseline: single large BOUNDED pass)
  U     : (no depth bound)                                 (true unbounded; only where safe)
  Tfin  : T + --finite-cycle-backedge                      (A/B: finite cycle rule, no collapse)

Usage:
  python3 scripts/m5_collapse.py --arms T,B,U,Tfin --only free-cell:537751 --out /tmp/m5_pilot.csv
  python3 scripts/m5_collapse.py --out /tmp/m5_sweep.csv          # full INSTANCES set
"""
import argparse, csv, json, os, subprocess, sys, threading, time

SOLVER = "cmake-build-release/bin/solvitaire"
RULES_DIR = "tests/rules"

# (label, rules-basename, seed, expected-from-oracle) — purposive set (plan §7).
INSTANCES = [
    # modest-depth UNWINNABLE cyclic (collapse should help most)
    ("free-cell_537751",      "free-cell.json",      537751,  "unsolvable"),
    ("delta-star_3605081",    "delta-star.json",     3605081, "unsolvable"),
    ("king-albert_1000379",   "king-albert.json",    1000379, "unsolvable"),
    ("somerset_1000068",      "somerset.json",       1000068, "unsolvable"),
    # modest-depth WINNABLE (ID should find a shallow solution fast)
    ("free-cell_1",           "free-cell.json",      1,       "winnable"),
    ("king-albert_1000010",   "king-albert.json",    1000010, "?"),
    # deep-tail outliers filled in at sweep time (see plan §7)
]

def read_vmhwm_kib(pid):
    try:
        with open(f"/proc/{pid}/status") as f:
            for line in f:
                if line.startswith("VmHWM:"):
                    return int(line.split()[1])
    except Exception:
        pass
    return 0

def run_one(argv, hard_timeout_s, ceiling_kib):
    """Run one solver child; return (stdout, peak_rss_kib, status) where status in
    {ok, timeout, mem-killed}."""
    p = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    peak = [0]; mem_killed = [False]; stop = [False]
    def mon():
        while not stop[0] and p.poll() is None:
            h = read_vmhwm_kib(p.pid)
            if h > peak[0]:
                peak[0] = h
            if ceiling_kib and h > ceiling_kib:
                mem_killed[0] = True
                p.kill()
                return
            time.sleep(0.05)
        h = read_vmhwm_kib(p.pid)
        if h > peak[0]:
            peak[0] = h
    t = threading.Thread(target=mon); t.start()
    status = "ok"
    try:
        out, _ = p.communicate(timeout=hard_timeout_s)
    except subprocess.TimeoutExpired:
        p.kill(); out, _ = p.communicate(); status = "timeout"
    stop[0] = True; t.join()
    if mem_killed[0]:
        status = "mem-killed"
    return out, peak[0], status

def arm_flags(arm, l0, big_bound):
    if arm == "T":    return ["--initial-depth-bound", str(l0), "--depth-grow", "2"]
    if arm == "B":    return ["--initial-depth-bound", str(big_bound)]
    if arm == "U":    return []
    if arm == "Tfin": return ["--initial-depth-bound", str(l0), "--depth-grow", "2", "--finite-cycle-backedge"]
    raise ValueError(arm)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--solver", default=SOLVER)
    ap.add_argument("--arms", default="T,B,U,Tfin")
    ap.add_argument("--only", default="", help="label:seed filter, e.g. free-cell:537751")
    ap.add_argument("--cache-capacity", type=int, default=4194304)   # 2^22 entries (~1-2 GB)
    ap.add_argument("--timeout-ms", type=int, default=300000)        # solver self-timeout
    ap.add_argument("--l0", type=int, default=1000)
    ap.add_argument("--big-bound", type=int, default=1000000)
    ap.add_argument("--ceiling-kib", type=int, default=10*1024*1024) # 10 GB hard kill
    ap.add_argument("--out", default="/tmp/m5.csv")
    args = ap.parse_args()

    arms = args.arms.split(",")
    insts = INSTANCES
    if args.only:
        lbl = args.only.split(":")[0]
        insts = [i for i in INSTANCES if i[0] == lbl]   # exact label match
        if not insts:
            print(f"no INSTANCES match label '{lbl}'", file=sys.stderr); sys.exit(2)

    hard_timeout_s = args.timeout_ms / 1000 + 30
    rows = []
    print(f"M5: cache-cap={args.cache_capacity} timeout={args.timeout_ms}ms L0={args.l0} "
          f"big-bound={args.big_bound} ceiling={args.ceiling_kib}KiB arms={arms}")
    for (label, rules, seed, expect) in insts:
        for arm in arms:
            argv = [args.solver, "--custom-rules", os.path.join(RULES_DIR, rules),
                    "--random", str(seed), "--json", "--force-lru",
                    "--cache-capacity", str(args.cache_capacity),
                    "--timeout", str(args.timeout_ms)] + arm_flags(arm, args.l0, args.big_bound)
            t0 = time.time()
            out, peak_kib, status = run_one(argv, hard_timeout_s, args.ceiling_kib)
            wall_ms = int((time.time() - t0) * 1000)
            verdict, states, removed, depth = "?", "", "", ""
            final_depth, solver_rss = "", ""
            if status == "ok":
                try:
                    d = json.loads(out)
                    verdict = d.get("solution_type", "?")
                    states = d.get("states_searched", "")
                    removed = d.get("states_removed_from_cache", "")
                    depth = d.get("max_depth", "")          # deepest node reached (≈ L at timeout)
                    final_depth = d.get("final_depth", "")   # solution depth (winnable)
                    solver_rss = d.get("solver_resident_bytes", "")
                except Exception:
                    verdict = "parse-error"
            else:
                verdict = status
            row = dict(label=label, seed=seed, arm=arm, verdict=verdict, expect=expect,
                       states=states, max_depth=depth, final_depth=final_depth,
                       peak_rss_mib=round(peak_kib/1024, 1),
                       solver_rss_mib=(round(int(solver_rss)/1048576, 1) if solver_rss else ""),
                       removed=removed, wall_ms=wall_ms, status=status)
            rows.append(row)
            print(f"  {label:24s} {arm:4s} -> {verdict:12s} states={states} "
                  f"max_depth={depth} final_depth={final_depth} "
                  f"peakRSS={row['peak_rss_mib']}MiB wall={wall_ms}ms ({status})")

    with open(args.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader(); w.writerows(rows)
    print(f"\nwrote {args.out} ({len(rows)} rows)")

    # Soundness check: definitive verdicts must agree per instance.
    from collections import defaultdict
    byinst = defaultdict(set)
    for r in rows:
        if r["verdict"] in ("winnable", "unsolvable"):
            byinst[r["label"]].add(r["verdict"])
    bad = {k: v for k, v in byinst.items() if len(v) > 1}
    if bad:
        print(f"\n*** RED-LINE ALARM: definitive-verdict disagreement: {bad} ***")
        sys.exit(1)
    print("Soundness: all definitive verdicts agree across arms.")

if __name__ == "__main__":
    main()
