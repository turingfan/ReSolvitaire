# PICKUP — depth-bounded-search branch

**Branch:** `claude/ecstatic-hopper-tpykG`
**Last updated:** 2026-06-07
**Phase:** **M1 GO.** Stage 0 complete; trace identity gate built + validated on x86_64.
**Stage 1 PR1 (items 1a–1d) COMPLETE + double-verified** (`9dcca88`; orchestrator +
fresh-worktree verifier, both PASS; red line confirmed across all 73 L1 unsolvables).
**Next: Stage 1 PR2 (items 1e + 1f).**

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
- **Trace identity gate READY** — full detail + recreate steps in
  [`trace-identity-reference.md`](trace-identity-reference.md). Reference = pristine
  `solvitaire-trace` from HEAD `45ccd43`, at session-local
  `/home/user/reference-bin/solvitaire-trace-ref-45ccd43` (SHA1 `894bcbb…`, not
  committed). Regenerate via `git worktree add /tmp/resolv-ref 45ccd43 &&
  (cd /tmp/resolv-ref && ./build.sh --trace)`; wire with
  `cmake -DTRACE_REF_BIN=<binary> cmake-build-trace`. Validated:
  `trace_regression_level1` = **150/150** (candidate == reference). The committed
  CMake default is ARM64 (useless here). **Decided (Ian):** keep session-local +
  documented reproducer.

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

> Read `implementation-plan.md` and `progress-log.md` (latest entry). PR1 is done +
> double-verified (`9dcca88`). Implement **Stage 1 PR2 (items 1e + 1f)** on branch
> `claude/ecstatic-hopper-tpykG`, **dispatching the implementer with `isolation:
> worktree`** (PR1 lesson). **1e:** outer iterative-deepening loop in `solve_game_impl`
> — loop `bounded_pass(L)`, grow `L` ×`--depth-grow` (default 2), **fresh cache per
> pass** (cross-pass reuse is Stage 2, NOT here), stop on SOLVED / UNSOLVABLE /
> `L ≥ L_max` / timeout. **1f:** differential-verdict harness — finite-`L` verdicts
> must match the unbounded oracle 100% on L1–L2 (reuse `regression_runner.py
> --compare-outcome-only`). **Gates:** `L=∞` identity must stay **150/150** (reference
> at `/home/user/reference-bin/solvitaire-trace-ref-45ccd43`; rebuild via
> `git worktree add /tmp/resolv-ref 45ccd43 && (cd /tmp/resolv-ref && ./build.sh
> --trace)` if absent — see `trace-identity-reference.md`); then verify with a fresh
> independent verifier subagent. Before building, install Boost
> (`sudo apt-get install -y libboost-program-options-dev`) — ephemeral per session.
> Keep `progress-log.md`/`PICKUP.md` current; open `BLOCKERS.md` + escalate (plan
> §1.2) on any soundness/semantic question instead of guessing. Do not start Stage 2.
