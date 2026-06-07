# Implementation Plan — Depth-Bounded, Cache-Reusing Iterative Deepening

**Branch:** `claude/ecstatic-hopper-tpykG`
**Status:** Plan for review — **no code until Ian approves Stage 0** (AGENTS.md: "no code until reviewed")
**Date:** 2026-06-07
**Governs:** the implementation of the design in [`proposal.md`](proposal.md),
with the author decisions recorded in proposal §1.5 / [`open-questions.md`](open-questions.md).

This plan is written for **Claude Code on the web** doing the implementation,
with **Ian asynchronous** (not constantly available). That single fact reshapes
the working rules from the interactive `AGENTS.md`: we cannot "stop and wait" for
an instant answer, so the discipline becomes **record every bug/blocker/question
clearly, never silently resolve or assume one away, and raise the genuinely
decision-shaped ones** (via `AskUserQuestion` and a tracked blockers log).

---

## 1. Execution model for Claude Code on the web (adapted AGENTS.md)

> AGENTS.md is written for interactive use on Ian's machine. The rules below are
> the **web-adapted** version. Where they differ from AGENTS.md, the web version
> wins for this project.

### 1.1 The async principle

The defining value of Solvitaire is that a reported **`unwinnable` is trusted**.
The whole risk of this project is silently breaking that (a wrong cycle/GHI rule
→ false `unwinnable`). Therefore, when working without Ian present:

- **Never silently resolve a soundness or domain/semantic question.** Record it,
  raise it, and do *not* let a later session assume "no reply" means "OK".
- **A noted blocker stays open** in `BLOCKERS.md` until Ian closes it. Carry it
  forward across sessions in `PICKUP.md`.
- **Make progress on independent work while a blocker is open** rather than
  idling — but never on the blocked item itself.

### 1.2 STOP-and-escalate vs proceed-and-record

| Situation | Action |
|---|---|
| **Soundness/semantic ambiguity** — how a rule maps to the domain (e.g. do dominance / K+ edges get `DEAD`/`OPEN` finalisation? does pinning `DEAD` interact with the "all entries live → `MEM_LIMIT`" rule? does the on-path set need uncached dominance states?) | **STOP that item. Escalate** (`AskUserQuestion` + `BLOCKERS.md`). A wrong guess can silently produce a false `unwinnable`. |
| **A differential-test disagreement** — any instance where the bounded/ID verdict ≠ the unbounded verdict | **STOP. Escalate.** Never "explain it away". This is the project's red line. |
| **Anything that could weaken the trust of a reported `unwinnable`** or the completeness guarantee | **STOP. Escalate.** |
| **A design choice not covered by proposal/this plan** that is architecturally significant or hard to reverse | **STOP. Escalate.** |
| **A pre-existing bug in existing code** | **Record as a finding + escalate.** Do *not* fold a silent fix into an unrelated change. |
| Mechanical choice fully determined by the spec | Proceed; note it in the PR description. |
| A bug in the agent's *own in-progress* code | Normal development; fix and continue. |
| Behaviour-preserving refactor / perf tuning that cannot change a verdict | Proceed in a *separate commit*; note it. |

When in doubt, escalate. Async cost of asking ≪ cost of a silent soundness bug.

### 1.3 Public artifacts (every session keeps these current)

| Artifact | Path | Purpose |
|---|---|---|
| This plan | `docs/depth-bounded-search/implementation-plan.md` | The contract. Update if reality diverges. |
| Progress log | `docs/depth-bounded-search/progress-log.md` | Append-only: what each session did, gate results, decisions taken. |
| Blockers / questions | `docs/depth-bounded-search/BLOCKERS.md` | Open questions for Ian; each with id, context, options, status. Created on first blocker. |
| Branch pickup | `docs/depth-bounded-search/PICKUP.md` | Resume state for the next session + a drafted next-session prompt. |

(AGENTS.md routes session logs to the `01-Knowledge-Base` repo and results to the
bench/`04-Results` repos. Those are **separate repos not in this session's scope**
— see §9. Until Ian wires them in, the above in-repo docs are the system of
record; raw measurement CSVs are an open logistics question, §8-D5.)

### 1.4 Commit / PR / branch discipline

- AGENTS.md "one commit per session" is an interactive rule; for web it becomes
  **one coherent, independently-reviewable unit per PR** (a stage or sub-stage),
  with clear messages and behaviour-preserving refactors split into their own
  commits.
- **Branch:** continue on `claude/ecstatic-hopper-tpykG` unless Ian opts for
  per-stage branches (§8-D1). Never push elsewhere without explicit permission.
- **WIP-backup branches (permitted exception, Ian 2026-06-07).** A
  `<branch>-wip-backup` ref MAY be pushed to durably snapshot an **in-flight
  subagent's uncommitted work** against container reclaim, when the orchestrator
  won't commit that WIP to the feature branch (unverified / possibly non-compiling).
  Push it **non-destructively** — build the snapshot via an isolated `GIT_INDEX_FILE`
  (`read-tree HEAD` → `add -A` → `write-tree` → `commit-tree -p HEAD`) so the
  subagent's working tree, real index, and HEAD are never touched. Delete the
  backup ref once the subagent's real commit lands on the feature branch — **but
  note the web git proxy may deny branch deletion (observed: HTTP 403, 2026-06-07).**
  If so, leave the (harmless, clearly-named) backup branch and flag it for manual
  cleanup with direct repo access rather than retrying.
  *Prevention:* prefer dispatching implementers with `isolation: worktree` (§3) so
  they never dirty the main tree in the first place — that avoids needing a backup.
- **PRs are the async review surface.** Open a PR per (sub-)stage **only once Ian
  has greenlit that stage** (the global rule is "no PR unless asked"; approving a
  stage = asking). The orchestrator may then offer to watch the PR for CI/review.
- Commit-message / artifact rule: **never** put the model identifier in commits,
  PR text, code, or comments.

### 1.5 Definition of done (applies to *every* code work item)

A work item is "done" only when **all** hold, verified by an *independent*
agent (§3), not the implementer's say-so:

1. Builds clean in **release + debug + trace** (`-Werror` is on).
2. **Gate 1** (release): `unit_tests` + `regression_level1` pass.
3. **Gate 3** (debug): `unit_tests` pass (catches UB / failed asserts).
4. If the item touches the search/cache: **Gate 2** (trace) passes, including the
   **`L = ∞` identity** (`trace_identity_*`, `trace_regression_level1/2`).
5. The item's **own new tests** pass and actually exercise the new behaviour.
6. The item's **acceptance criteria** (listed per item below) are met.
7. Progress log + PICKUP updated; any blocker recorded.

The standard local command is `python3 scripts/run_tests.py` (all three gates);
`--quick` for unit-only, `--gate <g> --skip-build` for one gate.

---

## 2. Mapping the algorithm onto the real engine (so work items are concrete)

Read on this branch — the integration points the stages touch:

- **DFS is iterative** with an explicit `frontier` (`solver.cpp:112` `dfs()`).
  Per loop: timeout/SIGINT → dominance push (`:133`) **or** cache-insert
  (`:145`–`:152` flat, `:166` LRU) → on new state, `get_legal_moves()` (`:175`);
  on already-present, backtrack (`:190`). Then `set_to_child()` + `make_move()` +
  `res.depth++` (`:205`), `res.max_depth` (`:207`).
- **Backtrack / subtree-complete** is `revert_to_last_node_with_children`
  (`:231`–`:282`); for LRU it clears the `live` bit via `set_non_live` (`:237`).
  **This is where Stage 2 finalises a node's `DEAD`/`OPEN(b)` status.**
- **Result type** enum `{TIMEOUT, SOLVED, UNSOLVABLE, MEM_LIMIT, TERMINATED}`
  (`solver.h:50`). JSON verdict mapping at `main.cpp:364`.
- **Run wrapper** `solve_game_impl<Policy>` (`main.cpp:68`–`116`) builds
  `game_state`, the cache (`:77`), the solver, and runs once (`:85`). **The outer
  ID loop wraps here.** `smart` retry (`main.cpp:334`–`354`) is the existing
  "run, inspect, re-run with a *fresh* cache" precedent.
- **Caches:** `cached_game_state{data, live}` + `cache.modify(...)` mutation
  (`global_cache.{h,cpp}`; `set_non_live` at `global_cache.cpp:273`); flat
  `insert_t` **no-ops on hit** (`generic_flat_cache.h:82`); `compact_state` byte 0
  = occupied, bytes 1–2 = depth (excluded from `matches()`), 3–31 = key.
- **CLI** options live in `command_line_helper.{h,cpp}` — new flags follow the
  existing `--timeout` / `--cache-capacity` pattern.

---

## 3. Subagent orchestration model

Ian invited subagents "if it makes sense" — it does, primarily to get
**independent verification** (the repo's own rule: *"verify claims by inspecting
the code, do not trust the other agent's summary"*). Roles:

| Role | Context | Responsibility |
|---|---|---|
| **Orchestrator** | the main web session for a stage | Sequences work items, runs the gates, keeps the public artifacts, surfaces blockers/questions to Ian, opens the PR. **Does not also self-verify.** |
| **Implementer** | subagent, `isolation: worktree` | Implements one work item from this plan's spec; builds; runs the minimal gates; returns a diff + self-report. |
| **Independent verifier** | fresh subagent, no stake | Reads the *actual diff* + this plan's acceptance criteria; **re-runs all gates from clean**; checks soundness assertions exist and tests exercise them; actively tries to *disprove* the implementer's claims. Returns pass/fail + evidence. |
| **Adversarial test-author** (Stage 2) | separate subagent | Writes the GHI/cycle tests (§4.3) *independently of* the implementer, so the tests aren't shaped to the implementation. |
| **Measurement runner** (Stage 0) | parallel subagents | Run probes per game/seed-set; return summaries. |

**Parallelism:** stages are **serial** (each gated by verifier + Ian). *Within* a
stage: Stage 0 probes parallelise across games; code work items that touch the
same files are serial, but a test-harness/measurement-script item can run in a
parallel worktree. Launch independent subagents in one batch.

**Handoff format** (implementer → verifier → orchestrator): (a) the work-item id;
(b) the diff; (c) commands run + raw gate output; (d) which acceptance criteria
are claimed met, with the evidence line for each; (e) anything noted/assumed.
The verifier re-runs (c) itself; it never trusts (d).

---

## 4. Testing & verification strategy (referenced by every stage)

### 4.1 The existing gates (reuse, do not reinvent)

- **3 required gates** (`python3 scripts/run_tests.py`): release `unit_tests` +
  `regression_level1`; trace `unit_tests` + `trace_*`; debug `unit_tests`.
- **Regression levels 1–5** via `scripts/regression_runner.py` against the
  oracles in `tests/oracles/`. Routine: L1–L2; pre-merge for a soundness-critical
  stage: L3 (and L4/L5 if Ian asks, they are long — see proposal regression table).
- **Trace gate** = the `L = ∞` identity proof. Needs reference binaries in
  `05-Executables/reference/` (§9 dependency).

### 4.2 New: the differential-verdict harness (the project's red line)

The one new piece of test infrastructure. For every oracle instance, the
bounded/ID solver's **final `winnable`/`unwinnable` verdict must equal the
unbounded verdict** — *any single disagreement is a hard failure and a STOP*.

- Implement as an extension of `scripts/regression_runner.py` (a `--depth-bound`
  / `-- id-mode` comparison run), reusing the oracle schema (`outcome`).
- Run at: finite small `L` (forces truncation/deepening) **and** `L = ∞` (identity).
- Report: verdict-agreement (must be 100%), solution-depth (expect ≤ unbounded),
  peak RAM on deep outliers (expect ↓), and the **unknown-count** at `L_max`.
- This harness is built **in Stage 1** and reused by Stage 2/3.

### 4.3 GHI / cycle adversarial tests (Stage 2, soundness-critical)

Hand-built minimal games with a deliberate back-edge and a **truncation hidden
behind the cycle** (proposal §3.5). Assertions: the solver must **not** report
`unwinnable` until `L` is large enough to expose the truncation. These tests
**must fail under either naive cycle rule** (taint-OPEN → never `unwinnable`;
closed-edge-∞ → false `unwinnable`). Written by the adversarial test-author
subagent.

### 4.4 Debug-build soundness assertions (proposal §7.3)

Compiled-in (debug) invariants, exercised by the gates: (a) a node finalised
`DEAD` had no truncated leaf and no unresolved back-edge below it; (b) a prune at
`OPEN(b)` only happened with `b ≥ B_now`; (c) `any_truncation == false` ⟺ root
finalised `DEAD`; (d) no `ON_PATH` marker survives a completed pass.

### 4.5 Unknown-count / CI budget

Track the fraction of instances ending `unknown` at the chosen `L_max`/time. For
any lossy variant (Stage 3) report the Wilson-CI widening explicitly and keep
within the paper's ±0.1% (±0.2% where already accepted).

---

## 5. The stages

> Each stage is independently shippable/testable; **do not start a stage until the
> previous one passes verification and Ian signs off** (proposal §5). Stage/work-item
> ids are stable references for the progress log and PRs.

### Stage 0 — Measure before building (no algorithm change)

**Goal:** get the cheap evidence for the depth-collapse hypothesis (proposal §1.3)
and a preliminary **go/no-go** (proposal §5 decision gate). This is the highest
value-per-risk work and unblocks confidence in the whole plan.

| Item | What | Files | Notes / acceptance |
|---|---|---|---|
| **0a** | Mine/emit per-instance: solution depth, max-depth of `unwinnable` proof, peak RSS, peak trail length, peak ancestor-pin count. Much exists (`res.max_depth`, `solver_resident_bytes` in `main.cpp`); add the missing counters. | `solver.{h,cpp}`, `main.cpp` (JSON) | **Must not change search behaviour** — verify `L=∞` trace identity still holds. Measurement only. |
| **0b** | A read-only **artificial depth-cap** probe: when `res.depth` would exceed a fixed cap, count a truncation and backtrack (no outer loop, no reuse). Run at several caps on the deep outliers; record the truncation-frontier depth vs full depth. | `solver.cpp` (guarded probe path) | This is a *throwaway* measurement switch, behind a flag; it previews Stage 1's cut. Acceptance: produces a cap→(truncations, verdict) table. |
| **0c** | Run 0a/0b across regression seed sets + hard games (Beleaguered Castle, Klondike, FreeCell, Spanish Patience); write a **measurement report**. | `docs/depth-bounded-search/stage0-report.md` | Parallelise across games (measurement-runner subagents). |

**Decision gate (Ian):** solution depths cluster shallow with a thin deep tail,
**or** deep outliers show a large snake-vs-cap gap → proceed. Broadly-distributed
deep solutions with no shallow truncation frontier → reconsider (lean on
constraint-based unwinnability instead). **The full collapse confirmation comes
after Stage 1/2** — Stage 0 gives the preliminary signal.

**Likely escalations:** how to measure "min arrival depth at scale" cheaply on a
190 M-node run (a side map may not fit) — propose sampling/counters and confirm.

**PR boundary:** one PR (instrumentation + report). Subagents: 2–4 measurement
runners (parallel) + verifier.

### Stage 1 — Sound bounded ID, fresh cache per pass (the safe core / scaffold)

**Goal:** the bounded cut + outer ID loop + new result type + the
differential-verdict harness. Delivers shallow-win-finding and the per-pass RAM
cap with **zero GHI risk and zero cache-format change**. **At `L0 ≈ 1000` this is
a correctness/measurement scaffold, not a shippable product** (a fresh cache
re-searches ~18×) — its job is to prove soundness and gather the truncation-frontier
data that firms up the go/no-go.

| Item | What | Files | Acceptance |
|---|---|---|---|
| **1a** | Add CLI: `--initial-depth-bound` (`L0`, default off/∞ so existing behaviour is unchanged), `--depth-grow` (default `2`), `--max-depth-bound` (default = tie to `--timeout`). | `command_line_helper.{h,cpp}` | Flags absent ⇒ byte-identical to today (trace identity). |
| **1b** | Add `BOUNDED_EXHAUSTED` to `solver_result::type` (`solver.h:50`) + `operator<<`. Carry `L` and `bool any_truncation` in `solver_impl`. | `solver.{h,cpp}` | Enum addition compiles everywhere `operator<<`/switches are used. |
| **1c** | The **depth cut**: in `dfs()`, before the dominance push (`:133`) **and** before legal-move expansion (`:175`), if `res.depth >= L` treat the node as a truncated leaf — set `any_truncation = true` and backtrack. | `solver.cpp` | At finite `L`, no node is expanded past depth `L`. |
| **1d** | Result mapping: exhausted ∧ `¬any_truncation` → `UNSOLVABLE`; exhausted ∧ `any_truncation` → `BOUNDED_EXHAUSTED`. Debug assert (4.4c). | `solver.cpp` | `BOUNDED_EXHAUSTED` never surfaces to JSON (consumed by 1e). |
| **1e** | **Outer ID loop** in `solve_game_impl` (`main.cpp:68`): loop `bounded_pass(L)`; on `SOLVED`/`UNSOLVABLE` return; on `BOUNDED_EXHAUSTED` grow `L ×= grow` and **rebuild a fresh cache** until `L ≥ L_max`/out-of-time → map to `timeout`/unknown. | `main.cpp` | Final JSON verdict mapping (`:364`) unchanged for winnable/unwinnable/timeout. |
| **1f** | The **differential-verdict harness** (§4.2). | `scripts/regression_runner.py` (+ a thin wrapper) | 100% verdict agreement vs unbounded on L1–L2 at small finite `L` *and* `L=∞`. |

**Validation gate:** (i) `L=∞` trace identity byte-identical; (ii) every
`SOLVED`/`UNSOLVABLE` verdict at finite `L` matches the unbounded verdict (1f);
(iii) equal-or-shallower solution depth and reduced peak RAM on deep outliers.

**Likely escalations:** confirm **depth units** — `L` counts the same counter as
`res.depth`, so K+ and dominance edges count toward depth (proposal §6.5,
recorded). Flag if any game makes this counter behave surprisingly.

**PR boundary:** two PRs — **1a–1d** (engine + CLI), then **1e–1f** (loop +
harness). Subagents: implementer + independent verifier per PR.

### Stage 2 — Cross-pass cache reuse: `DEAD` retention + `OPEN(b)` + `g_min` (LRU first)

**Goal:** the **heart of the project** and the main payoff — persistent `DEAD`
retention produces the cross-pass **depth collapse** (proposal §1.3). Built on
**`LRUPolicy` first** (decision Q6) using the **`DEAD` bit + `OPEN` side info**
(decision Q4). This stage carries the soundness risk; it is split so the risky
part (GHI/cycles) is isolated and independently tested.

> **Soundness rule for this whole stage:** the DFSTT3 cycle/shorter-path backup
> (proposal §3.5) is the one place an implementer must **not** improvise. A
> back-edge to an `ON_PATH` ancestor contributes the ancestor's **current finite
> estimate**, never `∞` and never a fully-closed edge. Any ambiguity here is a
> STOP-and-escalate (§1.2).

| Item | What | Files | Acceptance |
|---|---|---|---|
| **2a** | Add `status` (`DEAD`/`OPEN`, incl. the monotone **`DEAD` bit**), `b` (verified budget, `uint32`), `g_min` (min arrival depth, `uint32`) to `cached_game_state`; add `set_dead` / an upsert mutating via `cache.modify(...)` (mirror `set_non_live`). **No cross-pass behaviour yet** — fresh cache per pass, so this must reproduce Stage 1 exactly. | `global_cache.{h,cpp}` | Stage-1 verdicts + trace identity **unchanged** (isolates the cache-format change from the algorithm change). |
| **2b** | The **reuse rule** + **DFSTT3 backup** + **on-path set**: (i) explicit `on_path` hash set of frontier Zobrist keys (`insert` on descend / `erase` on backtrack) for `O(1)` ancestor detection independent of eviction; (ii) replace the "already present → backtrack" branch (`solver.cpp:190`) with the §3.3 decision (`DEAD`→prune; `OPEN(b)`: prune iff `b ≥ B_now` and `d ≥ g_min`, else re-open; `ON_PATH`→finite cycle contribution); (iii) accumulate per-node `verified = min(1 + child contributions)` and **finalise `DEAD`/`OPEN(b)`** in `revert_to_last_node_with_children` (`:231`); (iv) **keep the cache across passes** in `solve_game_impl` (move cache construction outside the ID loop; add `solver::run(L)` that resets `frontier` but not the cache). | `solver.{h,cpp}`, `main.cpp`, `global_cache.{h,cpp}` | Debug asserts 4.4(a)(b)(d) compiled in and green. |
| **2c** | **Pin `DEAD` against eviction** + prefer evicting shallow/low-budget `OPEN`: extend the LRU eviction loop (which already skips `live`, `global_cache.cpp:232`) to also retain `DEAD`. | `global_cache.cpp` | The cross-pass collapse measurably fires (node-count ↓ vs Stage 1). |
| **2d** | **GHI/cycle adversarial tests** (§4.3), authored independently. | `tests/...` (new unit tests) | Tests fail under both naive cycle rules; pass with the DFSTT3 backup. |

**Validation gate (soundness-critical — Ian sign-off required):** (i) identical
`winnable`/`unwinnable` verdicts vs **both** Stage 1 and current Solvitaire on all
oracle instances (L1–L3); (ii) measurable **total-node reduction** across the ID
sequence vs Stage 1 (reuse + collapse working) — *this is the empirical answer to
the central hypothesis*; (iii) no increase in unknown count; (iv) GHI tests green;
(v) peak-RSS drop on deep outliers.

**Likely escalations (escalate, do not guess — all soundness/semantic):**
- Do **dominance / K+ edges** get `DEAD`/`OPEN` finalisation, or pass their child's
  status through? (They are *not cached* today and are forced singletons; budget
  accounting through them must be defined.)
- Interaction of **pin-`DEAD`** with the "all entries live → `MEM_LIMIT`" rule and
  with the cross-pass persistence (a pass may start with a cache full of pinned
  `DEAD`).
- **Stale `ON_PATH`** markers if a pass ends mid-search (timeout/mem) — generation
  stamp (proposal §3.8) vs explicit clear.

**PR boundary:** four PRs (2a; 2b; 2c; 2d), stacked. 2d (tests) may be developed in
parallel with 2b in a separate worktree by the adversarial test-author. Subagents:
implementer + independent verifier per PR; separate test-author for 2d.

### Stage 3 — Optional churn control (only if Stage 2 churn is too high)

Each variant behind a flag, each reported with unknown-count + CI widening:
generation-aging tuning; the **lossy half-depth filter** (proposal §3.6 —
incomplete, therefore **opt-in only; the complete `b<B_now` mode remains the
default/required mode** per decision Q2); Luby-style restarts that keep the
retained terminal cache. **Do not start without Stage 2 churn data justifying it.**

---

## 6. Milestones & approval gates (Ian sign-off points)

| M | Gate | Ian decision |
|---|---|---|
| **M0** | This plan reviewed | Approve / amend → unblock Stage 0 |
| **M1** | Stage 0 report | **Go/no-go** on the collapse hypothesis |
| **M2** | Stage 1 verified (1a–1f) | Sign off the bounded core + harness |
| **M3** | Stage 2a verified | Sign off the cache-format change (verdicts unchanged) |
| **M4** | Stage 2b+2d verified | **Soundness gate** — the cross-pass reuse + GHI handling |
| **M5** | Stage 2c verified + collapse measured | **Firm answer to the central hypothesis**; sign off |
| **M6** | Stage 3 (optional) | Only if churn data justifies it |

At each milestone the orchestrator updates `progress-log.md` + `PICKUP.md`, carries
open blockers forward, and drafts the next-session prompt.

---

## 7. Risk register (delta from proposal §8 + execution risks)

| Risk | Severity | Mitigation |
|---|---|---|
| Wrong cycle/GHI handling → false/never `unwinnable` | **High** | Mirror DFSTT3 exactly (proposal §3.5); GHI tests (4.3) authored independently; isolate in 2b; soundness asserts (4.4) |
| Async agent silently resolves a soundness/semantic question | **High** | §1.2 STOP triggers; `BLOCKERS.md`; independent verifier checks "no undocumented assumptions" |
| Verifier not actually independent (rubber-stamp) | Medium | Fresh-context subagent; re-runs gates from clean; must cite evidence per criterion; never reads the implementer's self-report as truth |
| Iterative-engine per-node `verified` backup subtly wrong (2b) | Medium | Debug asserts 4.4; differential vs Stage 1; small hand-traced cases |
| Reference binaries / `05-Executables` absent → trace gate can't run | Medium | Confirm with Ian (§9); regenerate per `05-Executables/reference/README.md` if needed |
| Measurement at 190 M scale doesn't fit a side map | Low | Sampling/counters; escalate the method in Stage 0 |
| Collapse fails to materialise | Medium | Bounded downside (~2× overshoot + reuse); measured at M1/M5; constraint-based route as complement |

---

## 8. Decisions needed from Ian (for the implementation phase)

These do not block writing the plan; they shape execution. Defaults proposed.

- **D1 — Branch strategy.** Continue all stages on `claude/ecstatic-hopper-tpykG`
  *(default)*, or per-stage branches off it? (Task currently pins this branch.)
- **D2 — Regression depth before a soundness merge.** L1–L3 *(default)*, or also
  L4/L5 (long: ~100 min / ~600 min) before M4/M5?
- **D3 — Subagent model.** Confirm the implement → independent-verify pipeline
  with a separate adversarial test-author for GHI *(default: yes)*.
- **D4 — PR cadence.** Stacked PRs per (sub-)stage *(default)*, or fewer larger PRs?
- **D5 — Where raw measurement data lives.** In-repo summaries only *(default)*,
  or point me at the bench/`04-Results` repo (and can this session add it, §9)?
- **D6 — `L_max` policy.** Tie to `--timeout` *(default)* or a separate depth cap?

## 9. Dependencies / environment notes

- **Trace regression** needs reference binaries in `05-Executables/reference/`
  (CLAUDE.md). Confirm they exist on this branch / regenerate if not.
- **`01-Knowledge-Base/` is a separate repo, not in this session's scope** (Ian:
  "not sure you can cope with two repos at once"). Until wired in, the in-repo
  `docs/depth-bounded-search/` artifacts (§1.3) are the system of record. If Ian
  wants logs/results in their AGENTS.md-designated repos, he can add them to a
  session's scope (`list_repos` / `add_repo`).
- **Build/test** per CLAUDE.md (`./build.sh`, `python3 scripts/run_tests.py`,
  container scripts for Linux). Memory: trace/regression runs want `-m 7g`.

---

## 10. Immediate next step

**Awaiting Ian's review of this plan (M0).** On approval, the first work is
**Stage 0** (measurement + go/no-go) — no algorithm change, highest value-per-risk.
No code will be written before that approval (AGENTS.md).
