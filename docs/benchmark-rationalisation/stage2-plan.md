# Stage 2 — Usability & Functionality: implementation plan

**Branch:** `benchmark-rationalisation`
**Parent plan:** `01-Knowledge-Base/Implementation-Plans/benchmark-rationalisation-plan-2026-05-29.md` (§5)
**Status:** not started. Stage 1 complete (see `PICKUP.md`).

Stage 2 makes benchmarks easy and *safe* to run, and makes the kill discipline
uniform so runs stop losing work. This file is the execution-level breakdown:
discrete tasks, dependencies, technical design, acceptance, and the agent brief.

The scope is **Python/shell + docs only** — no C++ changes. The
`ReSolvitaire-bench` repo is OUT OF SCOPE (don't edit it). The 3 build/test gates
are not affected by these changes; Stage 2's real acceptance test is a clean
`bench_multiplicity.sh` run (see §Acceptance).

---

## Task DAG

```
SPINE (sequential — all touch run_benchmark.py / benchmark_orchestrator.py):
  T1 kill helper + run_benchmark integration  (+ folds in T4 vocab)
   └─> T2 orchestrator per-chunk timeout
        └─> T3 bounded, memory-aware concurrency + guard rails (+ T5/T6)

PARALLEL (independent; can run in worktrees alongside the spine or after):
  T7 remote-run docs (reconcile setup paths)        [docs only]
  T8 compare_benchmarks.py vs R decision            [eval → decide → maybe prune]
  T9 redux/unwinnable dedup decision                [diff → decide → maybe prune]
  T10 CLAUDE.md smart-solvability doc fix           [trivial]

FINAL (after spine):
  T11 integration acceptance run of bench_multiplicity.sh + START-HERE refresh
```

Conflict note: T8/T9 may `git rm` scripts and T3 edits the orchestrator; run the
script-pruning tasks (T8/T9) and the spine on separate commits and let the
orchestrator integrate in order to avoid worktree merge churn.

---

## T1 — Shared kill discipline (the linchpin)

Create one reusable runner used by both `run_benchmark.py` and the orchestrator.
Suggested home: `scripts/bench_lib/process.py` (new package) with a function like:

```
run_with_deadline(cmd, *, solver_timeout_s, grace_s, sigterm_grace_s,
                  capture=True) -> RunResult(returncode, stdout, stderr,
                                             wall_us, disposition)
```

Design requirements:
- **Own process group:** spawn with `start_new_session=True` (POSIX
  `setsid`) so the solver *and* any `/usr/bin/time` wrapper share a group.
- **Solver `--timeout` is authoritative.** Wait for natural exit up to
  **1.5× the solver timeout** (D1) — i.e. `total_wait_s = 1.5 × solver_timeout_s`,
  no 60s floor, no cap. Only if that elapses does the wrapper act.
- **Escalation on overrun:** `os.killpg(pgid, SIGTERM)` → wait `sigterm_grace_s`
  (≈30s fixed) → `os.killpg(pgid, SIGKILL)` → reap. Never SIGKILL before SIGTERM+grace.
- **Always capture partial stdout** and return it, regardless of how the process
  ended. No path discards already-emitted JSON.
- **Disposition** the caller can map to the CSV vocabulary:
  `EXITED_OK` / `EXITED_ERR` / `KILLED_AFTER_SIGTERM` / `KILLED_HARD`.
- No orphaned grandchildren: after return, the group is gone.

Then wire `run_benchmark.py` to it and fix its classification so:
- solver self-reported `"timeout"` in JSON → `TIMEOUT` (clean, has stats);
- wrapper had to SIGTERM but parsed partial JSON → `TERMINATED`;
- no output at all → `KILLED`.

**Folds in T4 (vocab completeness):** ensure no real outcome is written as a bare
`UNKNOWN`. The solver's `"failed"` JSON should map to a defined value (e.g.
`ERROR`/`FAILED`), not `UNKNOWN`. Document (in `csv_schema.md`) that the bench
hook does not count `UNKNOWN`, so emitting it would hide rows.

RSS caveat: with the process group, killing on overrun may cost the
`/usr/bin/time` RSS line. Acceptable: fall back to the solver JSON's
`solver_resident_bytes`; record `resident_memory_bytes` as blank/0 when the time
line is unavailable. (Do NOT silently write a wrong number.)

**Tests:** unit tests against fake child processes (a sleeper that ignores SIGTERM
vs one that exits cleanly; one that prints partial output then hangs) verifying
disposition, partial-capture, and that no process survives. These are
script-level pytest/unittest tests, independent of the C++ gates.

**Acceptance:** the helper SIGTERMs a wedged child at the deadline, captures its
partial stdout, classifies correctly, leaves no surviving processes; existing
`run_benchmark.py` CLI behaviour and CSV columns unchanged for normal runs.

---

## T2 — Orchestrator per-chunk timeout

`benchmark_orchestrator.py:run_chunk` currently calls `subprocess.run(cmd, ...)`
with **no timeout** → a wedged chunk hangs a worker forever. Give each chunk a
Python-side ceiling (derive from the chunk's solver timeout × seeds-in-chunk +
margin, or a configurable `--chunk-timeout`). On overrun, terminate the chunk via
the T1 helper (process group), record the chunk as failed, and keep the pool
moving. **Depends on T1.**

**Acceptance:** an artificially-wedged chunk is terminated and reported without
stalling the rest of the run.

---

## T3 — Bounded, memory-aware concurrency + guard rails (safety-critical)

In `benchmark_orchestrator.py` (and apply the same ceiling logic to
`bench_multiplicity.sh`):
- **Stop defaulting to `cpu_count()`.** Conservative default (e.g. a small fixed
  number or `min(cpu_count//2, max_safe)`).
- **Memory awareness applies always (D2)**, not only with `--cache-capacity`:
  assume **~3 GB resident per worker**; compute `max_safe = floor(available_RAM ×
  fraction / 3GB)`; final workers = `min(requested, hard_cap, max_safe)`. Print the
  computed limit + reason. Be lenient — **warn**, don't hard-refuse, for ordinary
  requests; escalate to a louder warning only when `--cache-capacity` is very large.
- **Account for nesting** (×~3–4 procs/worker) in process-count reasoning.
- **Engine (D3): use GNU `parallel`.** Drive workers via `parallel` with `--jobs`
  (the computed cap), `--memfree` (≈3 GB, as a second safety net), and optionally
  `--load`. Replace the orchestrator's `multiprocessing.Pool` and
  `bench_multiplicity.sh`'s `xargs -P` with `parallel` invocations. Record in the
  inventory + START-HERE; note the `parallel` dependency in START-HERE prerequisites.
- **Guard rails (T5):** the full `GAME_CONFIGS_FULL` matrix (14×4×500×20min) must be
  explicit opt-in; default to a bounded/quick scope; keep `--dry-run`.

**Depends on T2.** **Acceptance:** default invocation cannot spawn hundreds of
processes or exhaust RAM; exceeding the safe limit requires explicit override and
prints a warning; `--dry-run` shows the planned worker count + memory budget.

---

## T6 — Local usability

Sensible defaults, a documented `--quick` smoke path, clear actionable errors when
a binary/oracle is missing. Largely realised via T3 + START-HERE; keep small.

## T7 — Remote-run story (docs)

One documented remote path. Reconcile the two setup scripts: `scripts/setup_remote.sh`
(solver repo) vs `ReSolvitaire-bench/bootstrap/setup-remote.sh` (bench repo, out of
scope). The experiment runs *through* `bench --detached` (already exists). Make it
copy-pasteable in START-HERE; don't duplicate the bench README. Docs only.

## T8 — `compare_benchmarks.py` vs R analysis (decision deferred from Stage 1)

Evaluate what `compare_benchmarks.py` (39 KB, marked DEPRECATED) does that the R
layer (`summary.R`/`benchmark.R`/`compare_labels.R`) does not. Then either retain it
with a documented distinct role or `git rm` it. Same quick pass over the other
`stage2-eval` helpers (`benchmark_baseline.sh`, `benchmark_speedup.sh`,
`generate_baseline.py`, `collect_results.sh`, `compare_binaries.sh`,
`extract_benchmark_results.py`) — keep, demote to `experiments/`, or remove. Update
`script-inventory.md`. No hasty deletion.

## T9 — redux/unwinnable dedup

Now that we understand the pairs, diff each properly and decide: merge the unique
capability into the kept script and remove the variant, or keep both with a one-line
header explaining the difference. Resolve the `unwinnable2.sh` read disagreement
noted in PICKUP. Update inventory.

## T10 — CLAUDE.md doc fix

`--streamliners smart` → `smart-solvability` in CLAUDE.md (the CLI token; verified in
`command_line_helper.cpp`). Trivial; bundle with any docs commit.

---

## Acceptance for the whole stage

1. One kill path; a real run never discards solver-reported work.
2. `bench_multiplicity.sh` (the motivating case) runs end-to-end — local and via
   `bench --detached` — with **no avoidable kills**: every instance ends `SOLVED` /
   `UNWINNABLE` / clean `TIMEOUT`, never `KILLED` for a reason the wrapper could have
   avoided. Run a small smoke (`--phase D --seeds 1-5 --games klondike`) after the
   spine, and a larger validation when binaries are built.
3. Worker concurrency bounded + memory-aware; default cannot crash the machine;
   over-limit requires explicit override.
4. `compare_benchmarks.py` decision made + documented; redux/unwinnable resolved.
5. START-HERE + script-inventory updated to match reality.

Note: requires built release variant binaries (`./build.sh --release`;
`solvitaire`, `-flat`, `-lru`, etc.) for the acceptance run — this is separate from
the 3 unit/regression gates, which these Python/doc changes don't touch.

---

## Resolved decisions (Ian, 2026-05-29)

- **D1 (grace):** internal solver timing is unreliable, so leeway must scale with the
  timeout. Total wait before the wrapper SIGTERMs = **1.5× the solver `--timeout`**
  (down from 2×). **No 60s floor and no cap** — just 1.5× the timeout, whatever its
  size. After SIGTERM, a modest fixed grace (≈30s) to flush, then SIGKILL the group.
- **D2 (memory budget):** the memory-aware worker ceiling applies **always**, not only
  when `--cache-capacity` is passed. Assume **~3 GB resident per worker** as the default
  soft budget. Be lenient (this is a guardrail, not a straitjacket) — **warn**, don't
  hard-refuse, for ordinary requests; only get stricter/louder when `--cache-capacity`
  is set very large (a "ginormous" cache is when the user must be careful). Compute
  `max_safe = floor(available_RAM × fraction / 3GB)` and surface it.
- **D3 (engine):** GNU `parallel` is approved. Use it as the worker engine
  (`--jobs`, `--memfree`, `--load`) for clean job control, signal handling, and
  built-in memory throttling. (`--memfree` complements the D2 ceiling.)

---

## Agent brief template (use for every Stage 2 agent)

> You are a lower-level execution agent under an orchestrator (Opus), in an isolated
> git worktree on `benchmark-rationalisation`. Do ONLY the assigned task.
> **Read first:** `01-Knowledge-Base/AGENTS.md`; the parent plan; this `stage2-plan.md`;
> `CLAUDE.md`; the relevant script(s) and `docs/benchmarking/active/csv_schema.md`.
> **Rules:** Don't rewrite/invent beyond the task; reuse patterns. On any bug or
> semantic/domain question, STOP and report — don't fix or guess. The
> `ReSolvitaire-bench` repo is OUT OF SCOPE. Touch only named files. Write tests where
> specified and run them; paste output. Commit on the branch (Co-Authored-By:
> Claude Opus 4.8 <noreply@anthropic.com>); do NOT push. Report file-by-file changes,
> the worktree path + branch, test output, and anything you stopped on.
