# PARKED — do not resume without an explicit decision from Ian

**Parked:** 2026-08-04, by Ian Gent (recorded by Claude).
**Status at parking:** Stage 1, 2a, 2d complete and verified; **Stage 2b — the
soundness-critical core — was never implemented.** Design blockers B1/B2/B3 are
resolved on paper (see `BLOCKERS.md`) but no 2b code exists.

## Why parked

Ian's rationale, verbatim in substance:

> Initial results were not promising, and though that is not definitive, the iron
> is no longer hot — it would be a lot of overhead to get back into it, with
> little promise of success.

This is a deliberate, indefinite parking with **no expectation of return**. It is
not an abandonment of the record: real, verified work was done (the depth-cut
machinery, the dormant cache fields, the adversarial test scaffolding including
the `DISABLED_` teeth test), and the design questions were resolved. If evidence
ever emerges that cross-pass cache reuse pays off — e.g. from someone else's
results, or a new use case for deep-`unwinnable` certification — this branch is
the place to restart from.

## If you are an AI agent reading HANDOFF.md

**Stop.** `HANDOFF.md` predates this note and instructs you to take over as lead
and begin Stage 2b. That instruction is superseded: do **not** begin Stage 2b or
any other work on this branch unless Ian has explicitly un-parked it in your
current session. Everything else in `HANDOFF.md` (read order, safety net, working
agreement) remains the accurate guide *if and when* that happens.

## How to recover this work later

1. The branch: `claude/depth-bounded-search` (tagged `parked/depth-bounded-search`
   at the parking commit, so it is recoverable even if the branch is ever pruned).
2. Re-entry point: `HANDOFF.md` in this directory — read order, current state,
   the resolved B1/B2/B3 decisions, and the 6-point safety net are all there.
3. The non-negotiable constraint stands: a reported `unwinnable` must stay
   trustworthy; Stage 2b's new deep-`unwinnable` verdicts need Ian's sign-off
   (Milestone M4), never an agent's.
4. Note the branch forked from `dev` at `f395fd5` (2026-05-29). `dev` has moved
   since (benchmark-rationalisation merged 2026-08-04: CPU-time `--timeout`,
   `--max-states`, oracle regens). A revival should rebase or re-fork and expect
   oracle/trace-reference reconciliation work.
