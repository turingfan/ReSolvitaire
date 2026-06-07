# Stage 0 Report — Depth Distributions & the Collapse Precondition

**Date:** 2026-06-07
**Branch:** `claude/ecstatic-hopper-tpykG`
**Phase:** Stage 0 (measurement, no algorithm change) — milestone **M1 go/no-go**
**Data:** [`stage0-data/`](stage0-data/) (raw CSVs); reproduce with
`scripts/experiments/stage0_depth_sweep.py` + `stage0_analyze.py`.

## Verdict (recommendation): **GO** — proceed to Stage 1

The structural **precondition** for the depth-collapse hypothesis (proposal §1.3)
is present and, for the original JAIR target game (Beleaguered Castle), strong.
Stage 0 cannot *prove* the collapse — that needs the bounded search itself — but
it shows exactly the pattern the scheme is designed to exploit, and **no evidence
against proceeding**. Caveats below are real and shape *expectations per game*,
not the go decision.

## Method

`solvitaire --type G --random S --json --timeout 6000`, seeds 1–50, four games,
default cache policy. The solver's own 6 s timeout makes deep instances
self-terminate and still report `max_depth`, so rabbit-holes are captured, not
lost. **Metric semantics:** winnable → `final_depth` = DFS solution length;
unwinnable → `max_depth` = proof depth (`final_depth` returns to 0); timeout →
`max_depth` = how deep the search snaked in 6 s. "Snake fraction" = timeouts with
`final_depth == max_depth` (pure monotonic descent, no backtracking — the JAIR
§5.1 "tall, thin tree").

**Method caveats:** (1) this measures the *DFS-order* depth (the snake), **not**
the shortest-path structure — so it shows the precondition, not the collapse;
(2) 6 s is short, so some "timeout" instances are merely slow, not infinitely
deep; (3) n=50/game is a preliminary sample.

## Results (seeds 1–50, 6 s)

| Game | outcome mix | winnable sol depth (med / max) | unwinnable proof (med / max) | timeout depth (med / max) | snake frac | deep resolved¹ | collapse ratio² |
|---|---|---|---|---|---|---|---|
| **klondike** | W35 U7 T8 | 90 / 119 | 11 / 23 | 68 / 210 | 0/8 | 0/42 | 1× |
| **beleaguered-castle** | W26 U16 T8 | 3 652 / 935 364 | **12 / 186** | 288 530 / 1 142 367 | 3/8 | 7/42 | **230×** |
| **free-cell** | W40 U0 T10 | 20 957 / 397 497 | — | 233 889 / 840 776 | 3/10 | 25/40 | 9× |
| **spanish-patience** | W29 U1 T20 | 89 314 / 786 559 | 8 / 8 | 789 236 / 870 076 | **20/20** | 23/30 | 11× |

¹ resolved instances with `max_depth > 10 000`.  ² median timeout depth ÷ median resolved depth.

## Interpretation

**Two regimes emerge:**

- **Shallow games (klondike).** Solutions ≤119, unwinnable proofs ≤23, *no* deep
  resolved instances; even its 8 timeouts stay shallow (≤210, 0/8 snake) — they
  are **wide** (breadth/backtracking) blow-ups, not deep snakes. Depth-bounding is
  **neutral** here: the easy mass finishes in pass 1 at small `L`, and the wide
  timeouts are not a depth problem (so depth-bounding neither helps nor hurts them).

- **Snake games (beleaguered-castle, free-cell, spanish-patience).** Deep timeouts
  that snake to 10⁵–10⁶, with a large gap to the resolved depths (9–230×). Spanish
  is the purest pathology: **20/20 timeouts are pure monotonic descent** — the
  search commits to a single line and never backtracks within 6 s.

**Two findings strongly support the primary goal (faster *unwinnable* proofs, §Q1=d):**

1. **Unwinnable proofs are shallow when they resolve.** Beleaguered Castle proves
   unwinnable at `max_depth` **≤186** (median 12) across 16 instances; klondike
   ≤23; spanish's one unwinnable at 8. So unwinnability tends to be a *shallow*
   property — exactly what a depth-bounded scheme can reach early, *if* the deep
   timeouts share that shallow refutation structure.
2. **The target game shows a 230× collapse gap.** Beleaguered Castle's resolved
   instances cluster shallow (median 1 252; only 7/42 deep) while its timeouts
   snake to a median 288 530. This is the "190 vs 190 M" structure, measured.

**The honest caveat (shapes expectations, not the decision):** free-cell and
spanish-patience are **broadly deep** — most *resolved* instances already need
depth 10⁴–10⁵ (25/40 and 23/30 deep), and their winnable solutions are deep
(median 21 k / 89 k). For these, depth is not a thin-tail artifact; it is the
game's normal operating depth. Whether their deep timeouts are *collapsible*
(states reachable by short paths, the snake being an ordering artifact — supported
by spanish's 20/20 pure descent) or *intrinsic* (genuinely long-only paths) is
**the key question Stage 0 cannot answer**. With `L0 ≈ 1000` and ×2 growth these
games will take ~7–11 passes to reach their resolved depths — affordable only with
Stage-2 reuse, as the plan assumes.

## What this confirms vs. what Stage 1 must still validate

- **Confirmed:** the snake pathology is real and pervasive (3/4 games); resolved
  depths ≪ timeout depths (the precondition); unwinnable proofs are shallow; the
  shallow/deep split across games means a single `L0` behaves very differently per
  game (argues for per-game `L0`, plan §6.5).
- **Still to validate (Stage 1, the artificial-cap / first bounded passes):**
  whether a depth bound actually *resolves* the deep timeouts at a shallower depth
  (the collapse firing), especially for free-cell/spanish where depth is pervasive.
  The decisive Stage-1 measurement: run a deep timeout instance at increasing fixed
  `L` and see whether its verdict is reached far below the unbounded snake depth.

## Implications for the plan

- **No change to the staged plan.** Proceed to **Stage 1** (sound bounded ID +
  fresh cache + differential-verdict harness). Use the deep timeout instances
  identified here (beleaguered-castle, free-cell, spanish-patience timeouts) as the
  Stage-1 collapse probes.
- **Per-game `L0` looks warranted** (klondike wants `L0` ~10²; the snake games
  ~10³–10⁵). Keep the `L0 ≈ 1000` default but expect to tune per game (plan §8-D,
  proposal §6.5).
- **Goal (d) is well-targeted:** the shallow unwinnable proofs + the 230× gap in
  Beleaguered Castle are the best evidence; the winnable-side benefit (goal a) for
  free-cell/spanish is the more uncertain, Stage-1-dependent part.

## Decision requested (M1)

Recommend **GO to Stage 1**. If you concur, the next session implements Stage 1
items 1a–1f (plan §5) — the first code change. Optional: confirm whether to add an
explicit Stage-0b artificial-cap probe before Stage 1, or fold that measurement
into Stage 1's bounded passes (my recommendation: **fold into Stage 1** — the cut
is the same mechanism, so a throwaway probe is redundant once 1c exists).
