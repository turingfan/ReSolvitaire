# PICKUP — depth-bounded-search branch

**Branch:** `claude/ecstatic-hopper-tpykG`
**Last updated:** 2026-06-07
**Phase:** **M1 GO given.** Stage 0 complete. **Trace identity gate built + validated
on x86_64** (own pristine reference). **Stage 1 PR1 (items 1a–1d) dispatched** to a
background implementer subagent; orchestrator to verify independently before blessing.

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
- **Trace identity gate READY.** Reference = pristine `solvitaire-trace` from HEAD
  `45ccd43`, at session-local `/home/user/reference-bin/solvitaire-trace-ref-45ccd43`
  (SHA1 `894bcbb…`, not committed). Regenerate any session via
  `git checkout 45ccd43 && ./build.sh --trace`; wire with
  `cmake -DTRACE_REF_BIN=<binary> cmake-build-trace`. Validated: `trace_regression_level1`
  = **150/150** with candidate == reference. The committed CMake default is ARM64
  (useless here). **Open for Ian:** commit an amd64 reference in-repo for durability?

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

> Read `implementation-plan.md`, `progress-log.md` (latest entry), and the in-flight
> **Stage 1 PR1** (items 1a–1d) on branch `claude/ecstatic-hopper-tpykG`. **First
> step: independently verify PR1** (read the actual diff, re-run the gates — do not
> trust the implementer's summary; repo rule). The **identity gate is ready**: build
> the reference if absent (`git checkout 45ccd43 && ./build.sh --trace`), then
> `cmake -DTRACE_REF_BIN=/home/user/reference-bin/solvitaire-trace-ref-45ccd43
> cmake-build-trace && (cd cmake-build-trace && ctest -R '^trace_regression_level1$')`
> → **must be 150/150** (proves `L=∞` byte-identity). Then do **PR2**: the outer ID
> loop (1e, fresh cache per pass) + differential-verdict harness (1f); finite-`L`
> verdicts must match unbounded 100% on L1–L2. Before building, install Boost
> (`sudo apt-get install -y libboost-program-options-dev`) — ephemeral per session.
> Keep `progress-log.md`/`PICKUP.md` current; open `BLOCKERS.md` + escalate (plan
> §1.2) on any soundness/semantic question instead of guessing. Do not start Stage 2.
