# PICKUP — depth-bounded-search branch

**Branch:** `claude/depth-bounded-search`
**Last updated:** 2026-06-07
**Phase:** **M1 GO.** Stage 0 complete; trace identity gate built + validated on x86_64.
**Stage 1 PR1 (items 1a–1d) COMPLETE + double-verified** (`9dcca88`).
**Stage 1 PR2 (items 1e + 1f) IMPLEMENTED + self-validated** (this session): outer
iterative-deepening loop + differential-verdict harness. All gates green — L=∞ identity
150/150; release/debug/trace `unit_tests` 248/248 each; `regression_level1` (+variants)
4/4; **1f L1 = 150/150 verdicts match (0 flips)**; 1f self-test catches a planted
mismatch (loud exit 1); all 6 loop smoke tests correct (incl. L_max→timeout red-line and
depth-grow=1 no-hang guard). **PR2 DOUBLE-VERIFIED + BLESSED (M2 reached)** — fresh
worktree verifier found no discrepancies; 1f flip-detection proven; adversarial probe clean.
**Stage 2 (night-shift):** 2a (cache-format, behaviorally inert) + 2d (GHI/cycle adversarial
tests) dispatched; **2b BLOCKED on Ian** — 3 soundness questions in `BLOCKERS.md`; 2c is
downstream of 2b. **NIGHT-SHIFT ACTIVE:** autonomous per
[`night-shift-protocol.md`](night-shift-protocol.md); **no `AskUserQuestion`**, blockers → `BLOCKERS.md`.
**Branch renamed** `claude/ecstatic-hopper-tpykG` → `claude/depth-bounded-search`
(old branch + `…-wip-backup` orphaned on remote — proxy 403 blocks deletion).

## State of play

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

> Read `implementation-plan.md`, `night-shift-protocol.md`, and `progress-log.md`
> (latest entry). PR1 done (`9dcca88`); **PR2 (items 1e+1f) implemented + self-validated**
> this session (outer ID loop in `solve_game_impl` + `scripts/differential_verdict.py`
> wrapper + `regression_runner.py` ID flags). **First, independently VERIFY PR2** with a
> fresh-context verifier subagent (repo rule: read the actual diff, re-run gates from
> clean — L=∞ identity 150/150 at `/home/user/reference-bin/solvitaire-trace-ref-45ccd43`,
> rebuild via `git worktree add /tmp/resolv-ref 45ccd43 && (cd /tmp/resolv-ref &&
> ./build.sh --trace)` if absent; run the 1f harness on L1 and confirm 150/150 + that the
> self-test catches a planted flip; run unit_tests suites **sequentially** — they share
> `/tmp/st_agree_*.trace`). Then proceed to **Stage 2** (the heart: cross-pass `DEAD`
> retention + `OPEN(b)` + `g_min`, LRU first) per plan §5 / §6 sub-items 2a–2d, each gated
> on the 6-point safety net; author the GHI/cycle adversarial tests in a SEPARATE subagent.
> Before building, install Boost (`sudo apt-get install -y libboost-program-options-dev`)
> — ephemeral per session. Keep `progress-log.md`/`PICKUP.md` current. **If running
> autonomously/overnight, follow [`night-shift-protocol.md`](night-shift-protocol.md)**;
> blockers → `BLOCKERS.md`; **no `AskUserQuestion`**. If Ian is available, escalate
> soundness/semantic questions (plan §1.2) instead of guessing.

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
