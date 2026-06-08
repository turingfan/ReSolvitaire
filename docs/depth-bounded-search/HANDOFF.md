# HANDOFF — take over as LEAD of the depth-bounded-search project

**You are taking over as the LEAD/orchestrator of the depth-bounded-search project**
on branch `claude/depth-bounded-search`. Ian (the author) made the assistant the lead
and is **asynchronous** — you own day-to-day decisions and only escalate genuine
soundness/semantic forks. Read this doc, then the linked docs, then drive.

> **Kickoff prompt (what Ian pastes to start you):** "Read
> `docs/depth-bounded-search/HANDOFF.md` and take over as lead of the
> depth-bounded-search project. Confirm the gates are green from clean, then begin
> Stage 2b per the resolved B1/B2/B3."

## Read order (all under `docs/depth-bounded-search/`)
1. **`PICKUP.md`** — the live resume pointer (start here).
2. **`BLOCKERS.md`** — B1/B2/B3 **RESOLVED** (the Stage-2b design decisions you implement); F1 **DEFERRED**.
3. **`implementation-plan.md`** — staged plan; §1 web-execution working agreement, §1.5 gates, §3 subagent model, §5/§6 Stage 2 work-items + milestones.
4. **`proposal.md`** — the design; §3–§4 DFSTT3 cross-pass reuse (the algorithm you're implementing), §1.5 decisions.
5. **`progress-log.md`** (newest entries) — what's been done, with verification evidence.
6. **`night-shift-protocol.md`** — autonomous rules when Ian is away.
7. **`trace-identity-reference.md`** — the L=∞ identity gate + how to rebuild the reference binary.
8. **`2026-06-08-morning-plan.md`** — the most recent decisions/plan record.

## Where the project is (2026-06-08)
- **Stage 1 — DONE + double-verified (M2).** PR1 (`1a–1d`): depth cut, `BOUNDED_EXHAUSTED`, sound result mapping. PR2 (`1e–1f`): opt-in iterative-deepening loop (**fresh cache per pass**), `differential_verdict.py` harness. L=∞ identity 150/150; finite-L verdicts == unbounded oracle.
- **Stage 2a — DONE + verified inert (M3-ready).** Dormant `dead`/`b`/`g_min` + `set_dead`/`update_open` on the LRU `cached_game_state` (`global_cache.{h,cpp}`). Non-key, count-based eviction ⇒ behaviorally inert. The substrate for 2b.
- **Stage 2d — DONE + verified.** `src/test/unit_tests/depth_bound_verdict_test.cpp` (engine-level: ID verdict == unbounded across an adversarial cycle/transposition battery) + `ghi_cycle_abstract_test.cpp` (pure-logic naive-vs-DFSTT3 demo). Includes a **`DISABLED_` cross-pass-reuse "teeth" test** that fails on a naive 2b (~49 false-`unwinnable`s today).
- **Decisions RESOLVED** (BLOCKERS): **B1=A**, **B2=A**, **B3=`live`-bit per-pass cycle detection**. **F1 deferred** (revisit with Ian, ideally while building 2b).
- HEAD `a15b023` + today's doc commits; all pushed. **Nothing else in flight.**

## YOUR NEXT TASK — Stage 2b (the heart) then 2c (plan §5/§6)
**2b = cross-pass cache reuse** (DEAD retention + OPEN(b) + g_min + DFSTT3 finite back-edge backup + cycle detection), **LRU first**. This is the **soundness-critical core** — implement exactly per the resolved decisions:
- **B1=A:** a forced *uncached* (dominance/K+) edge contributes `1 + child_b` to its nearest **cached** ancestor's `verified`. **Fold upward — never key finalisation on the absent `cache_state` iterator** (the trap: silently dropping it ⇒ a parent finalised `DEAD` over an unexplored region ⇒ false `unwinnable`). Ply-metric is uniform (K+ = 1 ply).
- **B2=A:** `DEAD` is a **soft pin** — eviction prefers shallow/low-`b` `OPEN`, then `DEAD`; `MEM_LIMIT` fires only when all remaining entries are `live`.
- **B3:** keep the existing `live` bit for **per-pass** cycle detection; guarantee **no stale `live` bit survives into a new pass** (zero-on-exit, or a generation stamp). Cache nodes persist for reuse; on-path state resets each pass.
- **When 2b lands, rename off `DISABLED_`** `DepthBoundVerdictTest.DISABLED_Stage2_ReuseAcrossPasses_MatchesUnbounded` — it **must go green** (it's the teeth that prove 2b didn't introduce a false verdict).

**2c** = pin `DEAD` (soft) against eviction + prefer evicting shallow/low-`b` `OPEN` (`global_cache.cpp`).

Suggested shape: stacked sub-PRs (2b reuse-rule + DFSTT3 backup + cycle handling; then 2c), each independently verified. Milestones M3 (2a sign-off, ready), **M4 (2b+2d — the soundness gate, needs Ian sign-off)**, M5 (2c + measured collapse).

## Working agreement (non-negotiable)
- **Red line:** a reported `unwinnable`/`unsolvable` must stay trustworthy. Never ship a change that could produce a false one. New deep `unwinnable`s (beyond the oracle's reach) that 2b enables are **flagged for Ian's sign-off**, not auto-declared.
- **6-point safety net — every committed code unit:** (1) clean release+debug+trace build (`-Werror`); (2) **L=∞ identity `trace_regression_level1` = 150/150**; (3) release `unit_tests` + `regression_level1` (+flat/hash_only/lru); (4) the **1f differential harness 150/150** + the **2d adversarial suite green** (incl. the un-`DISABLED_`'d teeth test once 2b lands); (5) debug `unit_tests` (asserts); (6) implementer → **independent verifier** (fresh context, from clean) → orchestrator spot-checks the critical line. Run `unit_tests` binaries **sequentially** (shared `/tmp/st_agree_*.trace`).
- **Subagents:** dispatch implementers with **`isolation: worktree`** (keeps the main tree clean; merge their verified worktree branch after). For 2b, also use a **separate adversarial reviewer**. Commit + push **every verified unit**; keep `progress-log.md` + `PICKUP.md` current.
- **Escalation:** Ian present ⇒ `AskUserQuestion` for genuine soundness/semantic forks; Ian away ⇒ `night-shift-protocol.md` (no `AskUserQuestion`; log to `BLOCKERS.md`; never guess on soundness).

## Environment quirks (ephemeral web container)
- Install Boost each session: `sudo apt-get install -y libboost-program-options-dev`.
- The session-local trace reference is lost on a fresh container — rebuild from commit `45ccd43` (see `trace-identity-reference.md`): `git worktree add /tmp/resolv-ref 45ccd43 && (cd /tmp/resolv-ref && ./build.sh --trace) && cp /tmp/resolv-ref/cmake-build-trace/bin/solvitaire-trace /home/user/reference-bin/solvitaire-trace-ref-45ccd43`. Wire with `cmake -DTRACE_REF_BIN=<ref> cmake-build-trace`.
- The git proxy **403s on branch deletion** and only accepts `claude/*` pushes. Orphaned `claude/ecstatic-hopper-tpykG` + `…-wip-backup` need manual deletion with direct repo access.
- `AGENTS.md` / `01-Knowledge-Base/` are **not** in the web checkout; `docs/depth-bounded-search/` is the system of record here.

## First moves
1. Read the docs above (esp. BLOCKERS decisions, plan §5/§6, proposal §3–§4).
2. **Rebuild from clean + run the 6-point net** to confirm green before changing anything (also rebuilds the trace reference).
3. Plan + implement 2b per B1/B2/B3 (implementer + independent verifier + adversarial reviewer; `isolation: worktree`).
4. Escalate to Ian (or `BLOCKERS.md` if he's away) on any soundness ambiguity not already settled. Revisit **F1** with Ian when you reach the finalisation/reuse code.
