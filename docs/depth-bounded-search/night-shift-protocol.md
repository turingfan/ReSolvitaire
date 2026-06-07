# Night-Shift Protocol — autonomous overnight execution

**Authorized:** Ian, 2026-06-07 ("make as much progress as feasible overnight…
independent… having the whole plan in mind… more than just 1e and 1f could be done").
**Branch:** `claude/depth-bounded-search`. **Applies:** whenever running without Ian
available (he is asleep / async). This operationalizes plan §1.1–§1.2 for the
**no-human-available** case.

---

## 0. Prime directive (the red line)

A reported `unwinnable`/`unsolvable` must remain **trustworthy**. Overnight, with no
one to ask, the standing rule is absolute: **never commit anything that could let a
bounded or cache-reusing run report `unsolvable` unsoundly, and never present a *new*
`unwinnable` result as authoritative without Ian's sign-off.** When unsure about
soundness, **do not guess** — log to `BLOCKERS.md` and move to other safe work.

## 1. No `AskUserQuestion` overnight

`AskUserQuestion` blocks the session waiting for a human who is asleep — it would
freeze the night shift. **Do not call it during autonomous running.** Every question
goes to `BLOCKERS.md` (id, context, options, what you'd recommend, what you did
instead) and work continues on independent items. Ian triages `BLOCKERS.md` in the
morning.

## 2. The automated safety net — every committed code unit MUST pass ALL of

1. **Builds clean**: release + debug + trace (`-Werror`).
2. **L=∞ identity**: `trace_regression_level1` = **150/150** (bound absent ⇒
   byte-identical to pristine `45ccd43`). Rebuild the reference if the container is
   fresh (see `trace-identity-reference.md`).
3. **Existing gates**: release `unit_tests` + `regression_level1` (+ flat/hash_only/lru
   variants); debug `unit_tests` (asserts).
4. **Differential-verdict harness** (once 1f exists): bounded / cache-reusing **final**
   verdicts == the unbounded oracle on L1 (and L2 where time permits). **Any single
   mismatch ⇒ reject the unit, do NOT commit, log to `BLOCKERS.md`.** This is the
   project red line as an automated test (plan §1.2).
5. **Soundness asserts** present and exercised (Stage 1 `any_truncation`; Stage 2
   `DEAD`/`OPEN(b)` finalisation invariants).
6. **Independent verification**: implementer subagent → *separate* verifier subagent
   (re-runs gates from clean, tries to disprove). Orchestrator then **spot-checks the
   one critical line with own eyes** (the 150/150; the harness 100%). Never bless on a
   subagent's say-so (repo rule).

A unit that cannot pass all six is **not committed**; the obstacle goes to `BLOCKERS.md`.

## 3. Stage 2 soundness nuance (read before trusting new results)

The differential harness only validates instances where the **unbounded** solver has a
verdict. Stage 2's *payoff* is resolving instances unbounded **times out** on — those
have **no oracle**. Therefore:

- It is SAFE overnight to **implement + verify** Stage 2 for **non-regression**
  (differential-clean on known-verdict instances + adversarial GHI/cycle tests + debug
  asserts + identity). Commit such verified code.
- It is NOT safe to autonomously **declare a new deep `unwinnable`** (beyond the
  oracle's reach) as established fact. Produce such results if they arise, but record
  them as **"pending Ian's soundness sign-off"** in the progress log / `BLOCKERS.md` —
  never as settled `unwinnable`.

## 4. Work order (do in sequence; defer any item that can't pass §2)

1. **Stage 1 PR2 — 1e + 1f.** Outer iterative-deepening loop (opt-in via
   `--initial-depth-bound`, identity-preserving; grow ×`--depth-grow`; **fresh cache
   per pass** — cross-pass reuse is Stage 2, NOT here; stop on
   SOLVED/UNSOLVABLE/`L_max`/timeout). **1f** = the differential-verdict harness, which
   becomes the safety net for everything after — build it carefully and have the
   verifier confirm it actually *detects* a planted verdict mismatch (a net that always
   passes is worse than none).
2. **Stage 2 (the heart) — plan §6 sub-items**, each gated on §2. Author the GHI/cycle
   **adversarial tests in a SEPARATE subagent** (independent of the implementer) — may
   run in parallel with the implementer (plan §3).
3. **Beyond Stage 2** only if reached and clearly safe.

## 5. Operating rules

- **Commit + push after EVERY verified unit** to `claude/depth-bounded-search` (the
  container is ephemeral; never hold unpushed verified work). Retry push with backoff.
- **Keep docs current after each unit:** `progress-log.md` (what + gate results),
  `PICKUP.md` (resume state), `BLOCKERS.md` (every deferral).
- **Dispatch implementers with `isolation: worktree`** (PR1 lesson) so the main tree
  stays clean; have them push their verified commit to the feature branch at the end.
- **Keep the wake-chain alive:** end each turn with a background subagent in flight, so
  its completion re-invokes you. If everything remaining is blocked or done, write a
  final summary to `progress-log.md` + `PICKUP.md` and **stop** (correct terminal state).
- **Parallelize** where the plan allows (e.g. Stage 2 implementer ∥ adversarial
  test-author) — launch in one batch.
- **Never** open a GitHub PR (not asked), touch other repos, or put the model id in any
  artifact.

## 6. Terminal conditions (end the night shift)

- All planned work through Stage 2 is done + verified; **or**
- Everything remaining is blocked on Ian (all in `BLOCKERS.md`); **or**
- A gate fails in a way that can't be safely resolved autonomously (log it + stop).

In every terminal case, leave `progress-log.md` + `PICKUP.md` accurate so Ian (or a
fresh session) sees exactly what was done, what's verified, what's pending sign-off,
and what's blocked.
