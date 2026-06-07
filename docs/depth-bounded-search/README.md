# Depth-Bounded, Cache-Reusing Iterative Deepening — Branch Documentation

**Branch:** `claude/ecstatic-hopper-tpykG`
**Status:** **M1 GO** — Stage 0 (measurement) complete; trace identity gate built +
validated on x86_64; **Stage 1 (depth cut + bounded result) in progress.**
**Date:** 2026-06-07

This folder holds the design documents for adding *depth-bounded iterative
deepening with cache reuse* to ReSolvitaire: a way to avoid disappearing down a
190-million-deep "rabbit hole" when a shallow solution (or shallow refutation)
exists, while preserving Solvitaire's defining guarantee that a reported
`unwinnable` is genuinely unwinnable.

## Documents

| File | Purpose |
|---|---|
| [`proposal.md`](proposal.md) | The full design proposal: theory, algorithm, staged implementation plan, code-touch points, correctness/testing strategy, risks, and open questions. **Start here.** |
| [`implementation-plan.md`](implementation-plan.md) | The detailed, web-execution implementation plan: working agreement for async/autonomous Claude Code on the web, subagent model, testing & verification strategy, per-stage work items with acceptance criteria, milestones, and decisions needed from Ian. |
| [`PICKUP.md`](PICKUP.md) | Branch resume state + drafted next-session prompt. **Read this first when resuming.** |
| [`progress-log.md`](progress-log.md) | Append-only session-by-session record (env fixes, baselines, Stage 0 sweep, M1 GO, gate setup). |
| [`stage0-report.md`](stage0-report.md) | Stage 0 measurement results + the GO recommendation. Raw CSVs in `stage0-data/`. |
| [`trace-identity-reference.md`](trace-identity-reference.md) | The `L=∞` trace identity gate: what it is, the reference binary, how to **recreate** it, and how to run it. |
| [`open-questions.md`](open-questions.md) | The seven decisions needed from the author — **all RESOLVED 2026-06-06**, with rationale and a feasibility investigation per decision. |

**Decisions (2026-06-06):** primary goal is faster *unwinnable* proofs via the
depth collapse (so Stage 2 is the heart); a complete mode is mandatory; absolute
budget `b` (no generations); the monotone **`DEAD` bit** layout (option C);
`L0 ≈ 1000` with `×2` growth; **start on the LRU cache**. The first *shippable*
configuration is therefore Stage 2 + LRU + small `L0` (Stage 1 with a fresh cache
is only a correctness/measurement scaffold at this `L0`). Full detail in
proposal §1.5.

## One-paragraph summary

Run depth-first search with a depth bound `L`. If a solution is found → winnable.
If the whole tree is exhausted *with no branch ever hitting `L`* → genuinely
unwinnable (identical to today's search). If some branches hit `L` and no
solution is found → *unknown within `L`*: increase `L` (e.g. double it) and search
again. Across passes we reuse the transposition table: nodes proven dead with a
fully-explored, untruncated subtree (`DEAD`) are valid forever; nodes only
searched to a finite remaining budget (`OPEN(b)`) are trusted as "no win" only
when re-reached with no more budget than before, and re-expanded otherwise. This
is a satisficing specialisation of the proven sound-and-complete IDA\*+TT
algorithm **DFSTT3** of Akagi, Kishimoto & Fukunaga (2010) — the very paper the
Solvitaire JAIR article already cites.

**Why this can help even deep *unwinnable* instances.** The 27-/190-million
search depths are largely an artefact of depth-first ordering, not intrinsic: DFS
snakes down a line and caches each state at its *deep* first encounter, so its
much shorter alternative paths are later skipped. A depth bound forces states to
be discovered via their *shortest* paths first; a *persisted* cache then prunes
the long paths in later passes (a deeper pass is cut at the shallow `DEAD` nodes
proven earlier). The depth needed is governed by the space's shortest-path
structure, not the snake length — so the same `unwinnable`/`winnable` results can
often be reached far shallower, with far less RAM. This collapse is the main
payoff, and the reason persistent cross-pass reuse (not bounding alone) is the
heart of the scheme.
