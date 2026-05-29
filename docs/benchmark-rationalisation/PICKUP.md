# PICKUP — benchmark-rationalisation branch

**Last updated:** 2026-05-29
**Branch:** `benchmark-rationalisation` (cut from `dev`; pushed to origin; NO PR yet)
**Plan:** `01-Knowledge-Base/Implementation-Plans/benchmark-rationalisation-plan-2026-05-29.md`
**Stage 2 detail:** `docs/benchmark-rationalisation/stage2-plan.md` (this folder)

## Why this branch exists

Rationalise + robustify the benchmark tooling in the solver repo. Two stages:
Stage 1 = cleanup/rationalisation (DONE); Stage 2 = usability + functionality
(kill-discipline, worker safety, etc. — NOT started). Driven by an orchestrator
(Opus) dispatching lower-level agents (Sonnet) in worktrees; orchestrator reviews
+ integrates + commits. Supervision style: parallel where safe. Commit freely on
the branch; one PR to `dev` per stage (no PR yet, by Ian's instruction).

## Stage 1 — COMPLETE (5 commits on origin/benchmark-rationalisation)

```
a27c017 docs(bench): move superseded archive docs to KB, refresh README
fcd8599 docs(bench): add START-HERE guide and surviving-script inventory
9e83f35 chore: gitignore .claude/settings.local.json and worktrees
cb3b8c0 docs(bench): authoritative CSV + outcome-vocabulary contract
fcceb6c chore(bench): Stage 1 Wave A — remove level4 one-offs
```

Landed:
- Deleted `scripts/benchmark_level4.py` + `scripts/analyze_level4.py` (one-offs; the
  latter SIGKILLed on timeout and discarded solver output → `nodes:0`).
- `docs/benchmarking/active/csv_schema.md` rewritten as the **authoritative CSV +
  `solution_type` contract** (the ReSolvitaire-bench hook depends on this vocabulary).
- New `docs/benchmarking/active/START-HERE.md` (which-script-for-which-job) and
  `script-inventory.md` (every surviving script: job/inputs/outputs/status).
- `.gitignore`: `.claude/settings.local.json` + `.claude/worktrees/` (closes a git trap;
  see memory `settings-local-json-git-trap`).
- Superseded docs relocated to `01-Knowledge-Base/Archive/benchmarking-legacy-docs/`;
  `docs/benchmarking/README.md` refreshed.

**Kept deliberately (Stage 2 decisions):**
- `tuesday-night-redux.sh`/`...2.sh` and `bench_level5_unwinnable.sh`/`...2.sh` —
  divergent pairs, kept as `dedup-candidate` (don't delete a variant until confirmed
  it adds nothing). NOTE: the two deletion-agent reads of `unwinnable2.sh` disagreed
  (one saw "only hash-only, rest commented"; the inventory says "≈ identical to v1") —
  diff it properly in Stage 2 before deciding.
- 7 overlapping comparison/collection helpers tagged `stage2-eval` (incl.
  `compare_benchmarks.py`, already marked DEPRECATED in source).

## Stage 2 — IN PROGRESS (T1/T8/T9/T10 landed)

Commits on top of Stage 1:
```
7286ad6 docs: fix CLAUDE.md streamliner token (smart -> smart-solvability)   [T10]
da7716b chore(bench): dedup redux/unwinnable experiment script pairs          [T9]
252c5c2 feat(bench): shared process-group kill discipline (T1)
15da815 chore(bench): remove deprecated compare_benchmarks.py                  [T8]
```
- **T1 (kill discipline) DONE** — `scripts/bench_lib/process.py` `run_with_deadline()`
  (process-group spawn, 1.5× deadline, SIGTERM→grace→SIGKILL to the group, always
  captures partial output, 13 unit tests pass). `run_benchmark.py` rewired; `"failed"`→
  `FAILED`; RSS fallback; `csv_schema.md` reconciled; conformance gaps #1/#3 resolved.
- **T8 DONE** — `compare_benchmarks.py` removed (deprecated; R layer covers it). Dangling
  doc refs to tidy later: `known-issues.md` #5/#6, `evaluation-checklist.md` line ~250,
  `design.md` §7/§10 (informational).
- **T9 DONE** — `bench_level5_unwinnable2.sh` removed (scratch one-off); redux pair kept
  with distinguishing headers. **Flagged for Ian:** `tuesday-night-redux2.sh` has a
  `$SOLVER` env-override flaw — all four variant vars resolve to the same binary if
  `SOLVER` is exported. Decide: distinct env-var names per variant?
- **T10 DONE** — CLAUDE.md `smart` → `smart-solvability`.

**Remaining Stage 2:** T2 (orchestrator per-chunk timeout), T3 (bounded memory-aware
concurrency via GNU `parallel`, ~3GB/worker — D2/D3), T6 (local usability), T7 (remote
docs), T11 (acceptance run of `bench_multiplicity.sh`). T2→T3 are the sequential spine
and should reuse `bench_lib.process`. See `stage2-plan.md`.

**Process note:** the harness creates agent worktrees from a STALE base (original `dev`
HEAD), not the branch tip — so agents' edits to files changed earlier on the branch
(e.g. `csv_schema.md`) conflict on integration. Mitigation: for code, cherry-pick/checkout
only the code files and let the orchestrator reconcile shared docs; or tell the agent to
`git merge benchmark-rationalisation` into its worktree first. Also: one agent (T8) ran in
the MAIN checkout, not a worktree — committed directly to the branch (harmless here).

## Findings carried into Stage 2 (from the contract agent + investigation)

1. **Kill discipline (the core fix).** Solver `--timeout` is authoritative and emits
   full JSON on clean timeout (`solver.cpp` deadline check → `main.cpp` JSON). Wrappers
   are the problem: `run_benchmark.py`'s SIGTERM goes to the immediate child only
   (docstring claims process-group) → solver grandchild under `/usr/bin/time` can be
   orphaned; `benchmark_orchestrator.py` has **no** Python-side per-chunk timeout.
2. **Worker safety (crash risk).** Orchestrator defaults `--workers=cpu_count()`;
   nesting (worker→run_benchmark→/usr/bin/time→solver ≈ 3–4 procs each) → hundreds of
   processes. RAM (flat-cache mmap) is the real ceiling. Has crashed a machine.
3. **Outcome vocab gap.** ReSolvitaire-bench hook (OUT OF SCOPE — don't edit) silently
   drops `UNKNOWN` rows; ensure `run_benchmark.py` never emits a bare `UNKNOWN` for a
   real outcome, and document the hook gap.
4. **Quick doc bug.** `CLAUDE.md` says `--streamliners smart` but the CLI token is
   `smart-solvability`.

## State / housekeeping

- **Gates:** Stage 1 touched only scripts/docs/gitignore — no build inputs — so the
  3 gates are unaffected (verified `run_tests.py`/`regression_runner.py` don't
  reference deleted files). Not re-run.
- **Knowledge-Base repo (separate git repo, has Ian's unrelated uncommitted work):**
  the plan, the Stage 1 dev-log (`Dev-Logs/2026-05-29-benchmark-stage1-cleanup.md`),
  the relocated `Archive/benchmarking-legacy-docs/`, and the `Archive/README.md` bullet
  are **uncommitted in KB** — for Ian to commit there. NOT touched by the orchestrator.
- Agent worktrees under `.claude/worktrees/` are harness-locked; they auto-clean.

## To resume

1. Read the plan + `stage2-plan.md` (this folder).
2. Stage 2 spine is sequential (kill helper → orchestrator timeout → concurrency);
   docs/decision tasks can parallelise. See stage2-plan.md for the task DAG.
3. Acceptance for Stage 2 = a real `bench_multiplicity.sh` run (the motivating case)
   completing with no avoidable kills + bounded, memory-aware workers.
4. Mandatory: AGENTS.md rules (bug→stop+report; semantic question→ask Ian; test before
   done). Keep agents on the envelope in stage2-plan.md §Agent-brief-template.
