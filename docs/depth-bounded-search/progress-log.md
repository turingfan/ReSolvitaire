# Progress Log — depth-bounded-search

Append-only. Newest entries at the bottom. One block per session/work-chunk.

---

## 2026-06-07 — M0 approved; Stage 0 kickoff

- Ian approved the implementation plan and **made the assistant lead**, with
  defaults D1–D6 accepted (revisit in hindsight if needed). `docs/depth-bounded-search/`
  confirmed as the official system of record. Reference-binary / `01-KB` repo
  dependencies: Ian will provide when they become important (flag at Stage 1).
- Phase now: **Stage 0 (measurement, no algorithm change).**
- First action: establish a clean **baseline build + gate run** in this fresh
  container before changing any code (so later breakage is attributable), and
  recon the existing instrumentation counters to spec items 0a/0b precisely.

### Groundwork done this session

- **Environment gap found + fixed (ephemeral).** Fresh container had **no Boost
  dev headers** (`find_package(Boost program_options)` would fail); only runtime
  libs present. OS is Ubuntu 24.04/`noble` (Dockerfile targets 22.04). Ubuntu
  archive reachable (two unrelated third-party PPAs 403). Installed
  `libboost-program-options-dev` (+`time`) → Boost 1.83 headers present, cmake now
  finds Boost. **This install does not survive container reclaim** → recommend a
  SessionStart setup hook so every web session has it (flagged to Ian).
- `build/`, root `solvitaire`, root `unit-tests` are **committed artifacts**
  (git-tracked), not built here. Fresh `./build.sh --release --unit-tests` run
  into `cmake-build-release/` — `solvitaire` builds clean; `unit_tests`/variants
  compiling.
- **Trace reference binaries absent** (`05-Executables/reference/` missing) —
  Stage 1 trace-gate dependency; Ian to provide when we reach it.
- **Stage 0 item 0a needs NO code:** the JSON already emits `final_depth`,
  `max_depth`, and `solver_resident_bytes` (peak RSS via `getrusage`, `main.cpp`
  ~385). For unsolvable, proof depth = `max_depth` (final_depth returns to 0).
- **Measurement pipeline validated** on `cmake-build-release/bin/solvitaire`
  (klondike seeds 1–3): e.g. seed1 unsolvable max_depth=23 (158 k states); seed2
  winnable depth=91; seed3 winnable depth=90, max_depth=92 (928 k states).
  Klondike is shallow → the deep tail is game-specific (hunt Beleaguered Castle
  et al. in the sweep).
- **Reuse, not reinvent:** Stage-0 sweep will use the existing
  `--random/--type/--json/--timeout` interface (as `regression_runner.py` /
  `run_benchmark.py` already do).

### Next (resumes on build completion)

1. Gate 1 baseline (release `unit_tests` + `regression_level1`) — establish green
   baseline before any code change. (Gate 2/trace deferred: needs reference binaries.)
2. Stage 0 sweep across games + seeds (deep-tail focus) → `stage0-report.md` → M1
   go/no-go. Parallel measurement-runner subagents per game.

### Gate 1 baseline + Stage 0 sweep — done

- **Gate 1 baseline GREEN** (clean starting point before any code change):
  release `unit_tests` pass (120 s); `regression_level1` + flat/hash_only/lru
  variants pass (27 s). Gate 2 (trace) deferred — needs reference binaries.
- **Stage 0 sweep complete.** 4 games × seeds 1–50, 6 s cap, run as 4 parallel
  background jobs (mechanical CLI loops — chose background Bash over LLM subagents
  as the simpler tool for a pure measurement sweep). Raw CSVs committed to
  `stage0-data/`; analysis via `stage0_analyze.py`.
- **Result → GO (recommended).** See [`stage0-report.md`](stage0-report.md).
  Headline: snake pathology real in 3/4 games; Beleaguered Castle (the JAIR
  target) shows shallow unwinnable proofs (`max_depth` ≤186) and a **230× gap**
  between resolved (median 1 252) and timeout (median 288 530) depths. Honest
  caveat: free-cell/spanish-patience are *broadly* deep (depth not a thin-tail
  artifact) — whether their deep timeouts collapse is the question Stage 1 settles.
- **Awaiting Ian: M1 go/no-go** before writing any Stage 1 code.

---

## 2026-06-07 — M1 GO; trace identity gate built (own x86_64 reference)

- **Ian gave M1 GO** ("build Stage 1") and chose **self-baseline** for the identity
  gate. Follow-up: the committed trace reference is **Linux ARM64** (`…-linux-arm64-…`)
  — wrong arch for this x86_64 web container — so Ian authorised **building our own
  reference from the current pre-change state before any code change.**
- **Reference built (pristine).** Confirmed zero diff in `src/`+`CMakeLists.txt` at
  HEAD `45ccd43`, then `./build.sh --trace` → snapshotted `solvitaire-trace` to
  **`/home/user/reference-bin/solvitaire-trace-ref-45ccd43`** (SHA1 `894bcbb…`).
  Kept **session-local, not committed**: `AGENTS.md`/`01-KB` are absent from this
  checkout so the authoritative "where binaries go" rule is unverifiable, and the
  visible convention (CMake default → external `05-Executables/`; `.gitignore` skips
  `cmake-build-*`) says binaries live outside the solver repo. Offered to commit it
  in-repo if Ian prefers (1-min change).
- **Reproducer (any future session regenerates the identical reference):**
  `git checkout 45ccd43 && ./build.sh --trace` → `cmake-build-trace/bin/solvitaire-trace`.
  Wire the gate with `cmake -DTRACE_REF_BIN=<that binary> cmake-build-trace`.
- **Identity gate validated on x86_64 (candidate == reference now → must pass):**
  - `trace_identity_flat` / `trace_identity_lru` / `trace_until_timeout` — PASS (determinism).
  - `SearchTraceTest.*` + `SearchTraceAgreementTest.*` (5 tests) — PASS (incl. the
    107 s HashOnlyVsFlat 50-seed agreement test).
  - **`trace_regression_level1` = 150/150 instances, every event matched** (up to
    212 591 events/instance; 41 s direct run). This is the real `L=∞` identity gate:
    after Stage 1, the bound-disabled rebuild must still be 150/150 to prove
    byte-identical search.
- **Stage 1 PR1 (items 1a–1d) dispatched** to a background implementer subagent
  (CLI bound flags + depth cut + `BOUNDED_EXHAUSTED` + result mapping), self-checked
  against the identity gate + release gates; orchestrator verifies independently
  before it's blessed. Outer ID loop (1e) + differential harness (1f) deferred to PR2.

### WIP-backup decision (process)

- **Problem:** the implementer ran in the **shared working tree** (not an isolated
  worktree), so its uncommitted edits keep the tree dirty → the stop-hook nags to
  commit, but committing unverified/possibly-non-compiling WIP would corrupt the
  subagent's own commit flow. Won't do that.
- **Decision (Ian, 2026-06-07):** **WIP-backup branches are permitted** for durably
  snapshotting an in-flight subagent's work against container reclaim. Recorded as a
  standing rule in `implementation-plan.md` §1.4.
- **Action:** pushed a **non-destructive** snapshot (isolated `GIT_INDEX_FILE`:
  `read-tree HEAD`→`add -A`→`write-tree`→`commit-tree -p HEAD`, never touching the
  subagent's tree/index/HEAD) to **`claude/ecstatic-hopper-tpykG-wip-backup`**
  (commit `8470b73`; 6 files, +165/-22, all within PR1 scope). Delete this ref once
  the implementer's real commit lands on the feature branch.
- **Prevention / lesson:** plan §3 already says implementers should use
  `isolation: worktree` — I didn't this time, which caused the dirty-tree churn.
  **Dispatch future implementers (PR2, Stage 2) with `isolation: worktree`** so they
  never dirty the main tree and no backup is needed.

### Stage 1 PR1 (items 1a–1d) — COMPLETE + double-verified

- **Implemented** (`9dcca88`): `--initial-depth-bound`/`--depth-grow`/`--max-depth-bound`
  flags (bound OFF by default); the depth cut in `dfs()`; `BOUNDED_EXHAUSTED` result;
  sound mapping (`exhausted ∧ ¬any_truncation → UNSOLVABLE`, else `BOUNDED_EXHAUSTED`);
  bound threaded CLI→`dispatch_solve`→`solve_game_impl`→`sol.run()`. No outer loop (PR2).
- **Definition of Done (§1.5) satisfied — TWO independent verifications, both PASS:**
  - *Orchestrator* (read actual diff incl. `revert_to_last_node_with_children`; re-ran
    identity gate; adversarial tests).
  - *Fresh-context verifier subagent* (`isolation: worktree`, all 3 configs built FROM
    CLEAN, all gates re-run).
  - **L=∞ identity:** `trace_regression_level1` = **150/150** byte-identical to pristine
    `45ccd43` (confirmed by both).
  - **Soundness red line:** klondike s1 (unsolvable, proof depth 23) → `unsolvable` only
    at L≥24 (state count identical to unbounded), `bounded-exhausted` at L≤23. Verifier's
    **exhaustive sweep: all 73 L1 unsolvable instances at bound = depth−1/depth/depth+1
    → 0 false `unsolvable`, 0 boundary failures**; 228 (solvable, tiny-bound) pairs → 0
    false `unsolvable`. The red line holds across the whole L1 unsolvable corpus.
  - **Cut+LRU under debug asserts** (`--force-lru -L 8`) → `bounded-exhausted`, no assert.
  - Release+debug+trace `unit_tests`, `regression_level1`(+variants), `trace_identity_*`
    all pass from clean.
- **Follow-up items (non-blocking, recorded for later):**
  1. `--solvability`/`--benchmark` silently **accept but ignore** `--initial-depth-bound`
     (those paths call `run()` with default `boost::none`). Benign now; a later PR may
     reject or honor it.
  2. `L` counts dominance/auto-foundation/K+ edges as plies (`res.depth++` is per-move).
     Correct for PR1; **Stage 2 must keep this in mind when budgets tie to depth.**
  3. `has_max_depth_bound_` is dead state until PR2; `--depth-grow`/`--max-depth-bound`
     parsed but unused until the PR2 loop.
  4. `states_searched` is **not monotone in the bound** (truncation reshapes cache
     interactions) — expected, does not affect verdicts.
- **Housekeeping:** added `.claude/worktrees/` to `.gitignore` (`b607fca`); the
  `…-wip-backup` branch could **not** be deleted — the web git proxy denied it (HTTP
  403). Left in place (harmless); needs manual cleanup with direct repo access.

### Next: Stage 1 PR2 (items 1e + 1f)

- **1e** outer iterative-deepening loop in `solve_game_impl`: loop `bounded_pass(L)`;
  grow `L` (×`--depth-grow`, default 2); **fresh cache per pass** (cross-pass reuse is
  Stage 2, NOT here); stop on SOLVED / UNSOLVABLE / `L ≥ L_max` / timeout → map the
  exhausted-bound terminal to timeout/unknown.
- **1f** differential-verdict harness: finite-`L` verdicts must match the unbounded
  oracle 100% on L1–L2 (reuse `regression_runner.py --compare-outcome-only` + the bound
  flag). The verifier's 73-instance sweep already prototypes this check.
- **Dispatch the PR2 implementer with `isolation: worktree`** (lesson from PR1).

## 2026-06-07 — Branch rename + night-shift kickoff

- **Branch renamed** `claude/ecstatic-hopper-tpykG` → **`claude/depth-bounded-search`**
  (Ian: "this branch should have a more meaningful name"; chose this name). Created at
  `d38330f`, pushed, now the working branch. Current-state docs repointed; this log's
  historical mentions left intact. The old branch and `…-wip-backup` remain orphaned on
  the remote — the web git proxy denies deletion (HTTP 403); needs manual cleanup with
  direct repo access.
- **Night-shift authorized** (Ian, bedtime): make maximal feasible *independent*
  progress overnight, whole-plan-aware, beyond just 1e/1f. Wrote
  [`night-shift-protocol.md`](night-shift-protocol.md) — the autonomous guardrails:
  red-line prime directive; **no `AskUserQuestion`** (it would freeze on an absent
  human → blockers go to `BLOCKERS.md`); a **6-point safety net** every committed unit
  must pass (clean 3-config build · L=∞ identity 150/150 · existing gates · the
  differential-verdict harness once 1f exists · soundness asserts · implementer→
  independent-verifier→orchestrator spot-check); the Stage 2 nuance (verify the *code*
  for non-regression vs. don't autonomously *declare* new deep `unwinnable` — flag those
  for Ian's sign-off); commit+push every verified unit; keep the wake-chain alive.
- **Mechanism:** this session continues autonomously, driven by the background
  subagent-completion wake-chain (implement → verify → commit → dispatch next). Heavy
  work in fresh-context subagents keeps orchestrator context bounded across compaction;
  durable state lives in these docs + git so a fresh session can resume at any point.
- **Kicked off PR2** (1e loop + 1f differential harness) as the first night-shift unit.

## 2026-06-07 — Stage 1 PR2 (items 1e + 1f) implemented + self-validated

**Scope:** outer iterative-deepening loop (1e) + differential-verdict harness (1f).
Stage 1 only — **no cross-pass cache reuse** (that is Stage 2). Implemented in an
isolated worktree off `claude/depth-bounded-search` (HEAD `70068cb`).

### 1e — outer ID loop (`src/main/main.cpp`, `solve_game_impl`)
- New `struct id_options {initial_bound, grow, max_bound}`; threaded through
  `dispatch_solve` (replacing the bare `depth_bound`) to **both** `dispatch_solve`
  call sites in `solve_game` (main solve + `smart` `run_again` retry).
- **Flag absent ⇒ verbatim single unbounded pass** (`if (!id_opts) { … return; }`)
  — the original PR1 single-pass body, so the L=∞ identity holds byte-for-byte.
- **Flag present ⇒ ID loop:** fresh cache + fresh initial game state per pass;
  total `--timeout` shared across passes (deadline computed once, each pass gets the
  remaining time). Per-pass mapping: `SOLVED`→winnable; `UNSOLVABLE`→unsolvable
  (sound: that pass exhausted with no truncation); `TIMEOUT`/`MEM_LIMIT`/`TERMINATED`
  →surface as-is; `BOUNDED_EXHAUSTED`→grow `L` and loop.
- **Growth + non-progress guard:** `next_L = L*grow`; if `grow<=1` or `next_L<=L`
  (overflow/no-progress) force `next_L = L+1`. Plus `--depth-grow < 2` is rejected
  up front in `solve_game` (logs an error, clamps to 2) — belt-and-braces so the
  loop can never spin forever.
- **L_max:** `--max-depth-bound M` stops deepening once `next_L > M`. Absent ⇒ no
  cap (deepen until timeout — D6 default). The explicitly-requested L0 pass always
  runs (L_max bounds *deepening*, not the initial bound).
- **SOUNDNESS RED LINE:** if the loop stops without a `SOLVED`/`UNSOLVABLE` pass
  (timeout or L_max while last pass was `BOUNDED_EXHAUSTED`), the verdict is remapped
  to **`TIMEOUT`** (JSON `"timeout"`), **never `unsolvable`** and never surfaced as
  `bounded-exhausted`. `--solvability`/`--benchmark` stay unbounded (unchanged).

### 1f — differential-verdict harness
- Extended `scripts/regression_runner.py` with `--initial-depth-bound` /
  `--depth-grow` / `--max-depth-bound` (append the ID flags to each solver call;
  guard: refuses `--enforce-node-counts` with ID since node counts legitimately
  differ under truncation). The existing comparison policy already hard-fails on a
  verdict OUTCOME FLIP and soft-passes when either side is `timeout`.
- New thin wrapper `scripts/differential_verdict.py` (default L1, L0=1000, ×2):
  drives the runner in verdict-only ID mode, parses its summary, prints a crisp
  `N/N verdicts match`, and propagates a **loud non-zero exit on any mismatch**.

### Self-validation (all VERBATIM in the PR2 report / handoff)
- Builds: release + debug + trace all **clean under `-Werror`**.
- **L=∞ IDENTITY `trace_regression_level1` = 150/150 PASS (52.99s)** (ref
  `45ccd43` vs candidate, flag absent). `trace_identity_flat|lru` +
  `trace_until_timeout` = 3/3 PASS.
- Release `unit_tests` **248/248**; `regression_level1` (+flat/hash_only/lru)
  **4/4 PASS**. Debug `unit_tests` **248/248**. Trace `unit_tests` **248/248**.
- **1f on L1 (L0=1000, ×2): 150/150 verdicts match, 0 OUTCOME FLIPs.** Breakdown:
  **146 definitive verdict matches** (all 74 unsolvable proven `unsolvable`; 72/76
  winnable found) + **4 sound timeouts** (free-cell s36, spanish-patience s6/s24/s45
  — deep-snake winnables whose unbounded `max_depth` is 4103/52649/165196-states/2605;
  the fresh-cache ID run explodes 34M–103M nodes and times out → `timeout`, **never a
  wrong verdict**). This is the expected Stage-1 fresh-cache re-search cost and is
  exactly the depth-collapse case Stage 2 targets.
- **1f self-test (net catches a planted mismatch):** ran the harness against a temp
  oracle with one entry flipped (`alpha-star_seed_34` claimed `solved` when truly
  `unsolvable`) ⇒ `[FAIL] OUTCOME FLIP: unsolvable (expected solved)`, **exit 1**.
  Correct single-instance oracle ⇒ PASS exit 0. Temp oracles live in `/tmp`; the real
  `tests/oracles/level1.json` was never modified (git clean) — nothing to revert.
- **Loop smoke tests (release `--json`):**
  - klondike s1 unbounded ⇒ `unsolvable`, max_depth 23 (matches documented proof).
  - klondike s1 `--initial-depth-bound 10` ⇒ deepens 10→20→40>23 ⇒ **`unsolvable`**
    (158295 states, max_depth 23) — resolves, NOT bounded-exhausted/timeout.
  - klondike s1 `--initial-depth-bound 1000` ⇒ `unsolvable` in one pass.
  - black-hole s1 (winnable, depth 51) `--initial-depth-bound 20` ⇒ deepens ⇒
    **`winnable`** (matches unbounded); black-hole s2 (unsolvable, depth 50)
    `--initial-depth-bound 20` ⇒ deepens ⇒ **`unsolvable`** (matches unbounded).
  - **L_max red-line:** klondike s1 `--initial-depth-bound 8 --max-depth-bound 16`
    (proof depth 23>16) ⇒ **`timeout`**, max_depth 16 — **NEVER `unsolvable`**.
  - **infinite-loop guard:** klondike s1 `--initial-depth-bound 8 --depth-grow 1` ⇒
    logs `--depth-grow must be >= 2`, clamps to 2, terminates `unsolvable` — **no hang**.

### Findings (non-blocking; no `BLOCKERS.md` — no soundness ambiguity)
- **Test-isolation gotcha (not a code bug):** `SearchTraceAgreementTest.HashOnlyVsFlat`
  writes hardcoded `/tmp/st_agree_{a,b}.trace`. Running two `unit_tests` binaries
  (e.g. release + debug) **concurrently** collides on those paths → spurious
  "cannot open trace B" / false divergences. Run unit_tests suites **sequentially**.
  Verified: the test PASSES in isolation on all three builds (248/248 each). My
  initial parallel run was the only thing that failed — fixed by serialising.
- No soundness/semantic ambiguity hit ⇒ no `BLOCKERS.md` opened.

### Stage 1 PR2 — COMPLETE + double-verified (BLESSED)

- **Orchestrator verification:** synced + read the actual `main.cpp` loop diff; confirmed
  every exit preserves the red line (`unsolvable` only from a pass that returned
  `UNSOLVABLE`; all unresolved stops remap to `TIMEOUT`; `last_out` provably set at the
  `L_max` check). Own re-run: identity **150/150**; klondike s1 `-L10→unsolvable`,
  `-L8 -M16→timeout` (not unsolvable), `--depth-grow 1→no hang`, `-L1000→unsolvable`.
  Read `differential_verdict.py` — a real check (hard-fails on outcome flips).
- **Fresh-context verifier subagent (isolated worktree, from clean):** **no discrepancies.**
  3 builds clean; release/debug/trace `unit_tests` 248 each; `regression_level1` 4/4;
  identity **150/150** (non-vacuous, real event compare); **1f L1 = 150/150, 0 flips**
  (147 definitive + 3 sound timeouts); **1f flip-detection PROVEN** (planted
  `alpha-star_seed_3` flip → `[FAIL] OUTCOME FLIP`, exit 1); adversarial tiny-bound
  (`-L3`) probe on 12 solvable instances → **0** false `unsolvable`. Code-read confirmed.
- **M2 (Stage 1 verified) — the bounded core + the 1f safety net are sound and proven.**
  The 1f harness now guards all of Stage 2.

## 2026-06-07 — Stage 2 scoping (night-shift): 2a + 2d safe; 2b blocked on Ian

Read plan §5 Stage 2 (items 2a–2d) + §7 risks. Classified for autonomous overnight work:

- **2a (cache-format: `status`/`DEAD`-bit, `b`, `g_min` + `set_dead`/upsert) — SAFE.**
  Behaviorally inert (acceptance: "Stage-1 verdicts + trace identity unchanged"); the
  identity + 1f gates verify inertness. **Dispatching the 2a implementer overnight**
  (commit-on-worktree; I verify + merge). Flag M3 (cache-format sign-off) for Ian.
- **2d (GHI/cycle adversarial tests, authored independently) — SAFE.** Encodes the
  soundness contract (verdict == unbounded oracle on cycle/GHI-prone inputs);
  guards 2b. **Dispatching the test-author overnight.**
- **2b (cross-pass reuse + DFSTT3 backup + on-path set) — BLOCKED on Ian.** Plan §5
  lists three "escalate, do not guess — all soundness/semantic" questions (dominance/K+
  edge budget accounting; pin-`DEAD` vs all-live→`MEM_LIMIT`; stale `ON_PATH` across
  passes). Per the red line + night-shift §1, these go to **`BLOCKERS.md`** for Ian's
  resolution — NOT guessed. **2c** depends on 2b ⇒ also deferred. A read-only analyst
  is grounding the `BLOCKERS.md` entries against proposal §3.5/§3.8.

