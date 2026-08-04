# PICKUP — depth-bounded-search branch

> **⛔ PARKED (2026-08-04): this project is parked indefinitely — read `PARKED.md` before acting on anything below.**


**Branch:** `claude/depth-bounded-search`
**Last updated:** 2026-06-08
**Phase:** **Stage 1 DONE (M2)** + **Stage 2a/2d DONE** — all independently verified (HEAD
`a15b023` + doc commits; all pushed). **B1/B2/B3 RESOLVED** (Ian, 2026-06-08): B1=A
(pass-through, `+1`), B2=A (soft-pin `DEAD`), B3=`live`-bit per-pass cycle detection.
**F1 DEFERRED.** **Stage 2b/2c are UNBLOCKED but DEFERRED** to a fresh focused session.

> ➤ **New session / new lead: read [`HANDOFF.md`](HANDOFF.md) FIRST** — the control doc (full
> state, resolved decisions, working agreement + gates, environment quirks, the 2b task).

**Orphaned remote branches** `claude/ecstatic-hopper-tpykG` + `…-wip-backup` need manual
deletion (web git proxy 403s on branch deletion).

## State of play

- [`HANDOFF.md`](HANDOFF.md) — **the new-lead control doc (read first to take over).**
- [`2026-06-08-morning-plan.md`](2026-06-08-morning-plan.md) — latest decisions/plan record.
- [`proposal.md`](proposal.md) — full design. **Author-approved** (Ian: "I agree
  with the current version of the proposal"). Decisions baked in at §1.5.
- [`open-questions.md`](open-questions.md) — all seven questions **RESOLVED** (Ian,
  2026-06-06).
- [`implementation-plan.md`](implementation-plan.md) — detailed staged plan,
  web-execution working agreement, subagent model, testing strategy. **Approved (M0).**
- [`night-shift-protocol.md`](night-shift-protocol.md) — **autonomous overnight rules:**
  prime directive (red line), the no-`AskUserQuestion` rule, the 6-point safety net every
  committed unit must pass, the Stage 2 "verify code vs trust new results" nuance, work
  order, and terminal conditions.
- [`trace-identity-reference.md`](trace-identity-reference.md) — the `L=∞` identity gate
  + how to recreate the reference binary in a fresh container.
- [`stage0-report.md`](stage0-report.md) — Stage 0 measurement + **GO**
  recommendation. **Awaiting Ian's M1 decision.** Raw data in `stage0-data/`.
- [`progress-log.md`](progress-log.md) — session-by-session record (env fix,
  Gate 1 baseline green, Stage 0 sweep).
- [`BLOCKERS.md`](BLOCKERS.md) — **B1/B2/B3 RESOLVED** (Ian, 2026-06-08): B1=A, B2=A,
  B3=`live`-bit per-pass cycle detection. **F1 DEFERRED** (revisit while building 2b). No open blockers.
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

> **Read [`HANDOFF.md`](HANDOFF.md) and take over as lead of the depth-bounded-search
> project.** It carries the full state, the resolved B1/B2/B3 decisions, the working agreement
> + 6-point gate net, the environment quirks, and the next task. **First confirm the gates are
> green from clean** (rebuild incl. the trace reference from `45ccd43`), then begin **Stage 2b**
> per B1/B2/B3 (implementer → independent-verifier, `isolation: worktree`), then **2c**.
> Escalate genuine soundness forks to Ian (or `BLOCKERS.md` if he's away); revisit the deferred
> **F1** while building 2b.

## PR2 quick-run (commands)

```bash
sudo apt-get install -y libboost-program-options-dev   # ephemeral per session
./build.sh --release --unit-tests && ./build.sh --debug --unit-tests && ./build.sh --trace
REF=/home/user/reference-bin/solvitaire-trace-ref-45ccd43
cmake -DTRACE_REF_BIN="$REF" cmake-build-trace
(cd cmake-build-trace && ctest -R '^trace_regression_level1$' --output-on-failure)  # 150/150
# 1f differential harness (L1):
python3 scripts/differential_verdict.py --exe cmake-build-release/bin/solvitaire \
    --initial-depth-bound 1000 --depth-grow 2          # => 150/150 verdicts match
# run unit_tests suites ONE AT A TIME (shared /tmp trace paths):
./cmake-build-release/bin/unit_tests ; ./cmake-build-debug/bin/unit_tests ; ./cmake-build-trace/bin/unit_tests
```
