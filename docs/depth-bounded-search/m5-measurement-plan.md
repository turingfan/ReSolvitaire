# M5 — Collapse Measurement Plan (RAM-safe)

**Branch:** `claude/focused-dirac-1hhhkv` · **Status:** PLAN — for Ian's review before any run.
**Date:** 2026-06-09

M4 is signed off (2b cross-pass reuse + ∞ cyclic collapse + 2c DEAD-pin eviction, double-verified,
red line intact). M5 must produce the **firm empirical answer to the central hypothesis**: does
depth-bounded iterative deepening with persistent cross-pass reuse reach the same
`winnable`/`unwinnable` verdict with **fewer states explored and far less peak RAM** than a single
deep search — especially on the deep-tail *unwinnable* games this project targets.

**This document is a plan only. No measurement is run until Ian approves it.** Ian's two
constraints (2026-06-09) drive the design:
1. **It must not exhaust the machine's RAM**, even in the "infinite" (unbounded) case.
2. **Prefer comparing a small limit with doubling against a large but *bounded* limit (e.g.
   1,000,000)** rather than against a truly unbounded run.

---

## 1. Machine envelope (measured)

- **RAM:** 15 GiB total, ~15 GiB free, **no swap** (so an OOM is a hard kill, not a slowdown).
- **Cores:** 4. Runs are single-threaded; we run **one instance at a time** (no parallelism) so
  peak RSS is attributable and bounded.
- `/usr/bin/time -v` is **unavailable** → peak RSS is read via
  `resource.getrusage(RUSAGE_CHILDREN).ru_maxrss` from a **fresh Python wrapper per solver run**
  (one child ⇒ `ru_maxrss` = that child's peak RSS, in KiB on Linux). Cross-checked against
  `/proc/<pid>/status:VmHWM` for the first few runs.

## 2. The RAM hazard and how we cap it

- **The dominant RAM consumer is the transposition table, and the default `--cache-capacity` is
  100,000,000 states** (`command_line_helper.cpp:186`). At ~200–400 B per LRU entry (state vector
  + Boost MultiIndex node overhead) that is **~20–40 GB → guaranteed OOM** on this 15 GiB box.
  **Every M5 run MUST pass an explicit `--cache-capacity`.**
- **Chosen cap: `--cache-capacity 4194304`** (2²², ~4.19 M entries). Estimated footprint
  ~1–2 GB — comfortably within 15 GiB with wide margin for the process, frontier, and OS.
  (We will confirm the real footprint in the pilot, §6, and lower the cap if a run approaches
  ~8 GB RSS.)
- **`--force-lru`** for all runs: the 2b/2c reuse + collapse live on the LRU path. This also avoids
  the **flat cache's multi-GB `mmap` virtual reservation** (the `-m 7g` note in `CLAUDE.md`),
  which is irrelevant to this measurement and a needless OOM risk.
- **`--timeout`** on every run (default 300 000 ms = 5 min) so nothing runs unbounded in time.
- **A monitor aborts any run whose RSS exceeds a hard ceiling (~10 GB)** — see the harness, §5.

## 3. Baselines and treatment (Ian's bounded-vs-bounded design)

Both arms run the **same engine** (default ∞ cycle rule, `--force-lru`, same `--cache-capacity`),
so the only variable is the *search schedule*. Three configurations per instance:

| Arm | Config | What it isolates |
|---|---|---|
| **T — treatment** | `--initial-depth-bound 1000 --depth-grow 2` (small L0, doubling ID, cross-pass reuse + collapse) | The proposed scheme. |
| **B — large bounded baseline** | `--initial-depth-bound 1000000` (single pass at L=10⁶; **no** doubling, so effectively one big bounded search) | "One deep search" — Ian's RAM-safe stand-in for unbounded. Terminates (truncates at 10⁶) and is cache-capped. |
| **U — capped unbounded (only where safe)** | no depth bound, same cache cap + timeout | The true upstream behaviour, included **only** for instances that resolve at modest depth so U cannot blow up; skipped (or expected-timeout) for the deepest outliers. |

Rationale for **B** as the primary control: a depth bound of 10⁶ is far beyond the proof depth of
all but the most pathological instances, so for "normal" games B reaches the **same verdict** as
unbounded in **one pass** — a clean control for "did ID+reuse beat one big search?" — while being
**guaranteed to terminate and cache-capped**, exactly addressing Ian's RAM concern. For the
deep-tail outliers (Stage-0 depths of 27 M / 190 M) even B truncates at 10⁶ and yields no verdict;
there the interesting result is **"T reaches a verdict that B/U cannot within the same RAM/time
budget"** (a qualitative collapse win), plus the peak-RSS gap.

**A/B on the cycle rule:** run T additionally with `--finite-cycle-backedge` on the cyclic
instances, to quantify the ∞-collapse benefit directly (the verifier already saw ~15× on
`british-canister`: 84 vs 1259 states).

## 4. Metrics (per run, to CSV)

- `solution_type` (winnable / unsolvable / timeout / mem-limit) — **verdicts must agree** across
  arms that reach a definitive result (a disagreement among definitive verdicts = red-line alarm).
- `states_searched` (work). For T this is the **final pass**; the harness also sums a
  **cumulative-states** figure across passes (parsed from per-pass stderr if available, else the
  final-pass count is reported with a note).
- **`peak_rss_kib`** (the headline RAM metric) via the getrusage wrapper.
- `wall_ms`, `final_L` / `passes` (T), `states_removed_from_cache` (eviction pressure), and whether
  the cache cap was hit.

The headline tables: **peak RSS (T vs B)** and **states (T vs B)** per instance, plus the
T(∞)-vs-T(finite) collapse ratio.

## 5. Harness (to be written as `scripts/m5_collapse.py` — not yet run)

Per (instance × arm): a **fresh** `python3` wrapper process runs exactly one solver child:

```python
import resource, subprocess, json, sys
# build argv with --json --force-lru --cache-capacity 4194304 --timeout T + arm flags
p = subprocess.run(argv, capture_output=True, text=True, timeout=hard_timeout_s)
rss_kib = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss   # single child ⇒ its peak
# parse p.stdout JSON for solution_type/states/…; emit one CSV row
```

A lightweight **RSS guard** (a thread polling `/proc/<pid>/status:VmHWM`, or running the child
under `ulimit -v ~12g` via `bash -c`) kills any child exceeding ~10 GB and records `mem-killed`,
so a mis-estimate can never take the box down. One instance at a time; no concurrency.

## 6. Pilot before the full sweep (mandatory)

Run **one** modest instance (e.g. a known-unsolvable L2 cyclic seed) through all three arms first,
to (a) confirm the getrusage/VmHWM peak-RSS numbers agree and are ~1–2 GB at the 4 M cap, (b)
confirm verdict agreement, (c) confirm the RSS guard fires correctly on a deliberately tiny ceiling.
Only then run the full instance set. **If the pilot shows RSS near the ceiling, lower the cache cap
before proceeding.**

## 7. Instance set (small, purposive — not a full regression)

~8–12 instances, one at a time, chosen to exercise the regimes (final list picked at run time from
the L2/L3 oracles + Stage-0 outliers):
- **3–4 modest-depth UNWINNABLE cyclic** games (free-cell / spanish-patience / canister-family
  `*_unwinnable` seeds) — where the DEAD-collapse should most help; expect T peak-RSS ≪ B.
- **2–3 modest-depth WINNABLE** games — ID should find a shallow solution fast; expect T states ≪ B.
- **2–3 deep-tail outliers** from the Stage-0 sweep (the 27 M / 190 M depth instances) — the
  headline qualitative case: does T reach a verdict where B/U cannot within budget, and at what
  peak RSS? Run under the cache cap + a longer (but finite) `--timeout`, RSS-guarded.

## 8. Acceptance / what "collapse confirmed" means

- **Soundness (gate):** no definitive-verdict disagreement between any arms (else stop + escalate).
- **Headline:** on the unwinnable cyclic set, **T peak RSS and T cumulative states are materially
  below B** (target: a clear multiplicative gap, not noise), and on ≥1 deep-tail outlier **T
  resolves where B/U time out** within the same RAM budget.
- **Negative result is still a result:** if the collapse does not materialise (T ≈ or > B), record
  it honestly — that is the firm answer M5 exists to produce, and it informs whether Stage 3 / the
  constraint route is worth pursuing.

## 9. Explicitly out of scope for M5

Full L4/L5 regression; flat/hash/predecessor reuse (deferred, BLOCKERS B4); multi-seed statistical
benchmarking (that is the `benchmark-python` harness, a later effort). M5 is a **targeted,
RAM-safe demonstration**, not a benchmark suite.
