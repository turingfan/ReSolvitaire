# Depth-Bounded, Cache-Reusing Iterative Deepening — Branch Documentation

**Branch:** `claude/ecstatic-hopper-tpykG`
**Status:** Design proposal (no code yet) — for author review
**Date:** 2026-06-06

This folder holds the design documents for adding *depth-bounded iterative
deepening with cache reuse* to ReSolvitaire: a way to avoid disappearing down a
190-million-deep "rabbit hole" when a shallow solution (or shallow refutation)
exists, while preserving Solvitaire's defining guarantee that a reported
`unwinnable` is genuinely unwinnable.

## Documents

| File | Purpose |
|---|---|
| [`proposal.md`](proposal.md) | The full design proposal: theory, algorithm, staged implementation plan, code-touch points, correctness/testing strategy, risks, and open questions. **Start here.** |
| [`open-questions.md`](open-questions.md) | Consolidated list of decisions needed from the author before implementation. |

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
