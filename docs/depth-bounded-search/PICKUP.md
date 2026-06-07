# PICKUP — depth-bounded-search branch

**Branch:** `claude/ecstatic-hopper-tpykG`
**Last updated:** 2026-06-07
**Phase:** Design + planning complete. **No code yet.** Awaiting Ian's review of
the implementation plan (milestone **M0**).

## State of play

- [`proposal.md`](proposal.md) — full design. **Author-approved** (Ian: "I agree
  with the current version of the proposal"). Decisions baked in at §1.5.
- [`open-questions.md`](open-questions.md) — all seven questions **RESOLVED** (Ian,
  2026-06-06).
- [`implementation-plan.md`](implementation-plan.md) — detailed staged plan,
  web-execution working agreement, subagent model, testing strategy. **For review.**
- No `progress-log.md` / `BLOCKERS.md` yet — created when implementation starts /
  on the first blocker.

## Key constraints carried forward

- **Async Ian.** Record every bug/blocker/semantic question; never silently
  resolve. STOP-and-escalate triggers: plan §1.2. Red line: a reported
  `unwinnable` must stay trustworthy.
- **No code until the plan is approved** (AGENTS.md), then Stage 0 first.
- Decisions: goal = deep-`unwinnable` collapse (Stage 2 is the heart); complete
  mode mandatory; absolute budget `b`; monotone `DEAD` bit (option C); `L0 ≈ 1000`,
  `×2`; **LRU cache first**.

## Open decisions for Ian (plan §8)

D1 branch strategy · D2 regression depth before soundness merge · D3 subagent
model · D4 PR cadence · D5 where raw measurement data lives · D6 `L_max` policy.

## Next-session prompt (draft)

> Read `docs/depth-bounded-search/implementation-plan.md`. If Ian has approved it
> (M0) and greenlit Stage 0, begin **Stage 0** only (items 0a–0c: measurement +
> go/no-go report) — no algorithm change. Use a measurement-runner subagent per
> hard game in parallel and an independent verifier. Keep `progress-log.md`,
> `PICKUP.md`, and (on any blocker) `BLOCKERS.md` current. Do not start Stage 1.
> Escalate any soundness/semantic question per plan §1.2 instead of guessing.
