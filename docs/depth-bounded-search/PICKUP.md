# PICKUP — depth-bounded-search branch

**Branch:** `claude/ecstatic-hopper-tpykG`
**Last updated:** 2026-06-07
**Phase:** Plan approved (M0). **Stage 0 (measurement) complete — no algorithm
code yet.** Awaiting Ian's **M1 go/no-go** on `stage0-report.md` before Stage 1.

## State of play

- [`proposal.md`](proposal.md) — full design. **Author-approved** (Ian: "I agree
  with the current version of the proposal"). Decisions baked in at §1.5.
- [`open-questions.md`](open-questions.md) — all seven questions **RESOLVED** (Ian,
  2026-06-06).
- [`implementation-plan.md`](implementation-plan.md) — detailed staged plan,
  web-execution working agreement, subagent model, testing strategy. **Approved (M0).**
- [`stage0-report.md`](stage0-report.md) — Stage 0 measurement + **GO**
  recommendation. **Awaiting Ian's M1 decision.** Raw data in `stage0-data/`.
- [`progress-log.md`](progress-log.md) — session-by-session record (env fix,
  Gate 1 baseline green, Stage 0 sweep).
- No `BLOCKERS.md` yet — none hit so far (created on first blocker).
- Tooling added: `scripts/experiments/stage0_depth_sweep.py`, `stage0_analyze.py`.
- Environment: Boost dev headers must be installed per session (ephemeral) — see
  SessionStart-hook recommendation in the log; build + Gate 1 confirmed working here.

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

> Read `implementation-plan.md` and `stage0-report.md`. **If Ian has given the M1
> go**, implement **Stage 1 only** (items 1a–1f, plan §5): CLI depth-bound flags,
> the depth cut + `BOUNDED_EXHAUSTED` + result mapping, the outer ID loop
> (fresh cache per pass), and the differential-verdict harness. Gates: `L=∞`
> trace identity must be byte-identical (needs the trace reference binaries — ask
> Ian); finite-`L` verdicts must match unbounded 100% on L1–L2. Use an
> implementer→independent-verifier subagent split. Before building, install Boost
> (`sudo apt-get install -y libboost-program-options-dev`) — ephemeral per session.
> Keep `progress-log.md`/`PICKUP.md` current; open `BLOCKERS.md` + escalate (plan
> §1.2) on any soundness/semantic question instead of guessing. Do not start Stage 2.
