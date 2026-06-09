# Depth-Bounded, Cache-Reusing Iterative Deepening — Branch Documentation

**Branch:** `claude/focused-dirac-1hhhkv`
**Status:** **Stage 2 COMPLETE + double-verified — M4 signed off (2026-06-09).** Stage 0
(measurement) done; trace identity gate built + validated; Stage 1 (depth cut + bounded
result) done; **Stage 2 (LRU cross-pass reuse + ∞ cyclic-region collapse + DEAD-pin eviction
+ teeth tests) done and independently verified twice.** Next: **M5 collapse measurement**
(planned in [`m5-measurement-plan.md`](m5-measurement-plan.md), RAM-safe bounded-vs-bounded).
**Date:** 2026-06-09

This folder holds the design documents for adding *depth-bounded iterative
deepening with cache reuse* to ReSolvitaire: a way to avoid disappearing down a
190-million-deep "rabbit hole" when a shallow solution (or shallow refutation)
exists, while preserving Solvitaire's defining guarantee that a reported
`unwinnable` is genuinely unwinnable.

## Documents

| File | Purpose |
|---|---|
| [`proposal.md`](proposal.md) | The full design proposal: theory, algorithm, staged implementation plan, code-touch points, correctness/testing strategy, risks, and open questions. **Start here** for the *why*. |
| [`implementation-plan.md`](implementation-plan.md) | The detailed web-execution implementation plan: working agreement, subagent model, testing & verification strategy, per-stage work items + acceptance criteria, milestones. |
| [`PICKUP.md`](PICKUP.md) | Branch resume state + next-session prompt. **Read this first when resuming.** |
| [`progress-log.md`](progress-log.md) | Append-only session-by-session record (Stage 0 sweep, M1 GO, Stage 1, Stage 2b/2c/2d, the ∞ decision, both independent-verifier passes). The authoritative **as-built** narrative. |
| [`BLOCKERS.md`](BLOCKERS.md) | The B1–B4 implementation traps + **F1** (the cycle back-edge rule: why +∞ "mark `a-s-a` dead" is the sound default — the prefix-reachability argument). |
| [`future-optimisations.md`](future-optimisations.md) | Search-saving opportunities deliberately deferred (with teeth-test requirements). O1 (cyclic collapse) is now the shipped default; O2 (suit-symmetry + reuse) remains open. |
| [`m5-measurement-plan.md`](m5-measurement-plan.md) | The **RAM-safe** M5 collapse-measurement design: bounded-ID vs a single large *bounded* baseline, capped cache, peak-RSS/states metrics, instance set. |
| [`night-shift-protocol.md`](night-shift-protocol.md) | Rules for autonomous overnight execution: the red-line prime directive, the 6-point safety net per committed unit, work order. |
| [`trace-identity-reference.md`](trace-identity-reference.md) | The `L=∞` trace identity gate: what it is, the reference binary, how to recreate + run it. |
| [`stage0-report.md`](stage0-report.md) | Stage 0 measurement results + the GO recommendation. Raw CSVs in `stage0-data/`. |
| [`open-questions.md`](open-questions.md) / [`HANDOFF.md`](HANDOFF.md) | The seven author decisions (all RESOLVED 2026-06-06) and the original session handoff. Historical. |

## CLI flags (as-built)

Depth-bounded iterative deepening is **off by default** (a normal run is byte-identical to
upstream). It is enabled per-invocation:

| Flag | Meaning |
|---|---|
| `--initial-depth-bound <L0>` | Enable bounded ID with first-pass depth bound `L0` (absent ⇒ unbounded `L=∞`, identical to upstream). A node whose depth reaches `L` is a truncated leaf. |
| `--depth-grow <k>` | Growth factor between passes (default 2 ⇒ doubling). |
| `--max-depth-bound <Lmax>` | Upper limit on `L` for iterative deepening (default tied to `--timeout`). |
| `--finite-cycle-backedge` | **A/B / fallback only.** Forces the *finite* DFSTT3 back-edge rule. Default (flag absent) is the **+∞ closed-edge** rule, which collapses cyclic-dead regions to `DEAD` across passes (see BLOCKERS F1). Both are sound. |

The cross-pass reuse + cyclic collapse currently apply on the **LRU** cache path (the default
for two-deck / spider-stock / suit-symmetry games, and forceable elsewhere with `--force-lru`).
The flat / hash-only / predecessor caches run a fresh cache per pass (sound; reuse extension
deferred under BLOCKERS B4).

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
