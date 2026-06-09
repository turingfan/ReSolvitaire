# M5 situation report — depth-bounded search measurements

**Date:** 2026-06-09 **Branch:** `claude/focused-dirac-1hhhkv`
**Author:** AI session (Opus) under Ian's direction
**Audience:** human (Ian) *and* AI pickup — written to be self-contained after a gap.

---

## 0. TL;DR (read this first)

On the two free-cell instances measured so far, **depth-bounded iterative-deepening
(ID) shows no benefit and is catastrophic on the winnable instance — in *both*
directions of the depth bound.** The only thing that helps is the **infinite-cycle
(`cost=∞`) rule**, which is ~7× on a shallow cyclic *unsolvable* instance and is
independent of depth bounding.

Ian's reaction (verbatim, paraphrased): *"kind of depressing… I'll have to look at
the code to see if there's some horrible mistake. If not, the next question is what
is causing this bad behaviour (or the good behaviour of the other case)."*

So there are **two live questions**:
1. **Is there a bug?** — does the depth-bounded path do something pathological
   (e.g. fail to cache/dedup under a bound, re-expand, lose the transposition
   table)? See §5 for exactly where to look.
2. **If not a bug, what is the mechanism?** — why does bounding free-cell explode
   the state count, and why does the `cost=∞` cycle rule collapse it? See §6.

**Nothing in `src/main` was changed in this work** — these are measurements only.
The behavioral state of the solver is whatever was on this branch before
2026-06-09 (no functional commits since; only `scripts/m5_collapse.py` and docs).

---

## 1. What M5 is testing

The depth-bounded-search proposal (`proposal.md` §1.3, the "depth-collapse
hypothesis") claims that running DFS under an iteratively-deepening depth bound `L`,
**with cross-pass reuse of `DEAD` nodes**, lets deep proofs collapse: shallow passes
prove regions `DEAD`, and those proofs cut later deeper passes cheaply. The headline
target was *unwinnable* games whose unbounded proof snakes very deep (the "190 vs
190 M states" structure from Stage 0).

M5 is the measurement gate: does the bounded scheme actually beat unbounded DFS on
real instances, and at what RAM cost?

## 2. Exact setup

**Harness:** `scripts/m5_collapse.py` (committed). One solver child per (instance ×
arm); a `/proc/<pid>/status` `VmHWM` monitor thread records peak RSS and hard-kills
any child exceeding `--ceiling-kib` (default 10 GB) so a mis-estimate can't OOM the
box. Parses the solver's JSON and records, per arm:
`verdict, states_searched, max_depth, final_depth, peak_rss, solver_resident_bytes,
states_removed_from_cache, wall_ms`.

**JSON metric semantics** (confirmed against the solver output):
- `max_depth` = deepest node reached in DFS order. **At a timeout under bound `L`
  this saturates at `L`** (the bound truncates), so it doubles as "which pass / how
  deep did it get".
- `final_depth` = solution length for a winnable verdict; returns to ~0 for
  unsolvable.

**Arms** (all share `--force-lru`, `--cache-capacity 4194304` (2²² entries),
`--type free-cell`, solver self-`--timeout`):

| Arm | Flags | Meaning |
|---|---|---|
| `U` | *(none)* | true unbounded DFS — `depth_bound = boost::none` |
| `B` | `--initial-depth-bound 1000000` | single large **bounded** pass (controls for "bounded vs unbounded code path") |
| `T` | `--initial-depth-bound L0 --depth-grow 2` | **treatment**: doubling ID + cross-pass reuse + `cost=∞` cycle collapse |

**Cycle rule:** always the **infinite cycle (`cost=∞`)**. The earlier finite-cycle
A/B arm (`--finite-cycle-backedge`, "Tfin") was **dropped** on Ian's instruction
("finite cycle can go arbitrarily deep — not tenable"). It is gone from the harness
defaults.

**Cross-pass reuse** is compile-time gated to `lru_cache` only
(`solver.cpp` / `main.cpp:186` `cross_pass_reuse = is_same_v<cache_type, lru_cache>`),
which is why every arm forces `--force-lru`.

## 3. Results

### 3a. `free-cell --random 537751` — cyclic **unsolvable** (shallow)

| Arm | verdict | states | max_depth | RAM |
|---|---|---|---|---|
| U | unsolvable | 54,939 | **93** | 7.5 MiB |
| B | unsolvable | 54,939 | 93 | 6.4 MiB |
| T (L0=10,000) | unsolvable | 54,939 | 93 | 6.4 MiB |
| ~~Tfin (finite cycle)~~ *(dropped)* | unsolvable | **389,120** | 93 | 7.3 MiB |

- The unsolvable **proof is shallow (depth 93)** → far below any `L0` tested, so
  every bounded arm runs a single pass identical to unbounded (`U = B = T = 54,939`).
- The only mover is the **cycle rule**: the dropped finite-cycle arm needed 389,120
  states; the default `cost=∞` rule does it in 54,939 (**~7.1×** fewer). This is the
  one clean win, and it is **depth-independent** (nothing to do with bounding).

### 3b. `free-cell --random 1` — **winnable** (deep DFS path)

| Arm | verdict | states | max_depth | wall | RAM |
|---|---|---|---|---|---|
| U (unbounded) | **winnable** | 86,992 | 28,453 | 0.7 s | 14 MiB |
| B (single L=10⁶) | **winnable** | 86,992 | 28,453 | 0.2 s | 15 MiB |
| T (L0=10,000, ×2) | **timeout** | 263,000,000 | 10,000 (stuck pass 1) | 240 s | 695 MiB |
| T (L0=100, ×2, **1 hr**) | **timeout** | **2,964,596,135** | 100 (stuck pass 1) | 3,600 s | 693 MiB |

**Depth-bounding fails in both directions and never completes even the first pass:**
- `L0=10,000`: 263 M states in 240 s, `max_depth` pinned at 10,000 → still
  truncating at the horizon, never finished pass 1, never doubled.
- `L0=100`: **2.96 billion** states in a **full hour**, `max_depth` pinned at 100 →
  the depth-≤100 region of this instance is itself astronomically wide and did not
  exhaust.
- Unbounded `U`/`B` resolve in **86,992 states / <1 s** by snaking straight down one
  path to the depth-28,453 solution.

The depth-28,453 figure is almost certainly a **DFS-order artefact** (the "snake"):
real free-cell solutions are short, so a shorter solution likely exists, but the
bounded search can't reach it because the shallow subtree is too wide to enumerate.

## 4. The one-line interpretation

Free-cell blow-ups are **wide, not deep** (this matches `stage0-report.md`). Unbounded
DFS wins by *not* exploring the width — it commits to one deep path. A depth bound
**forces** breadth-ish enumeration of the shallow subtree, which is enormous. So:
- small `L` → drowns in width before reaching any solution;
- large `L` → can't reach the deep solution the snake found;
- the bounded search **is** the width blow-up Stage 0 warned about.

## 5. If it's a bug — where to look (for the human pass)

Before trusting the "wide subtree" story, rule out a pathology in the bounded path.
Concrete suspects, with pointers:

1. **Is dedup/caching actually working under a bound?** The transposition table must
   still suppress revisits when `depth_bound` is set. If a node is cached with its
   *depth* (or the depth-bound logic invalidates entries), the same state could be
   re-expanded at different depths → state-count explosion. **Look at:**
   `src/main/solver/solver.cpp:139–151` (the truncation cut) and `:209–281` (the
   bounded cycle/DEAD/`cost=∞` block), and how cache hits interact with
   `res.depth >= *depth_bound`. Does a truncated/OPEN node get cached in a way that
   lets it be re-searched?
2. **`any_truncation` / OPEN-ancestor marking.** `solver.cpp:144–151`: a truncated
   leaf marks every ancestor OPEN (never DEAD). Verify that "OPEN because truncated"
   doesn't *also* defeat ordinary transposition dedup for states that are genuinely
   re-reachable below `L`. If OPEN nodes are never cached as hits, the bounded search
   loses the transposition table entirely → that alone would explain 2.96 B states.
3. **Single-pass sanity.** In 3b both T runs are stuck in **pass 1** (`max_depth ==
   L`). So this is *not* a cross-pass-reuse bug — it's a single bounded pass already
   exploding. Compare `B` (single L=10⁶ pass, 87 k states, fine) vs `T L0=100` (single
   L=100 pass, 2.96 B states). **Same code path, only `L` differs** → whatever is
   wrong (or right) is in how the cut at small `L` interacts with caching, not in the
   ID loop. This is the cleanest A/B to debug: `B` vs `T L0=100` on `free-cell 1`.
4. **Is `B` really bounded?** Confirm `--initial-depth-bound 1000000` exercises the
   *bounded* code path (`depth_bound` set, just never hit at depth ≤28,453). If `B`
   silently behaves like `U`, then we have *no* evidence the bounded path is correct
   at any `L` — `B` would not be a valid control. Check `main.cpp` arg parsing →
   `solver.cpp:103 depth_bound = bound`.

If all four check out, it's not a bug and §6 is the real question.

## 6. If it's not a bug — candidate mechanisms (for either pass)

- **Transposition dedup degrades with depth.** Under a tight bound, many states are
  first *reached* via a truncated/OPEN path and cached as non-final; when reached
  again deeper they may be re-expanded. The shallower the bound, the more states are
  "OPEN, revisit allowed" → super-linear blow-up. (This is the leading hypothesis and
  overlaps with §5.2.)
- **DFS path-commitment is doing real work.** Unbounded DFS's 87 k-state win may rely
  on diving to a goal before the width matters. Removing that (via a bound) removes
  the only thing keeping the search small. If so, depth-bounding is fundamentally
  wrong for wide/winnable games and should be gated to *narrow, deep-unwinnable*
  games only.
- **`cost=∞` good case:** on `537751` the cycle rule finalises cyclic regions `DEAD`
  with `verified = +∞` (`solver.cpp:226–243`), pruning the ~7× of states the finite
  estimate would re-walk. Worth confirming *which* states those 334 k pruned states
  are (cyclic re-entries?) to characterise when the rule pays off.

## 7. Next steps

**For the human (Ian):**
- Eyeball `solver.cpp:139–151` and `:209–281` for the dedup-under-bound question
  (§5.1/§5.2). The decisive experiment is already isolated: **`B` vs `T L0=100` on
  `free-cell 1` is the same single bounded pass with different `L`** — if `B` (L=10⁶)
  is 87 k states and `T` (L=100) is 2.96 B, the divergence is purely "what the cut at
  small L does to caching".
- Decide whether depth-bounding is worth pursuing at all for wide games, or should be
  scoped to deep-unwinnable only (which Ian has said is "not the crux now").

**For an AI pickup:**
- Do **not** re-run the 1-hour `T L0=100` blindly — the result is recorded here. The
  cheap, decisive debug run is `B` vs `T L0=100` on `free-cell 1` with a **short**
  timeout (say 10 s) plus tracing of cache hits/misses and how many distinct states
  are inserted vs re-expanded. If the solver can log "cache hit suppressed / OPEN
  revisit allowed" counts under a bound, that directly answers §5.2.
- Add an instrumented counter (unique inserts vs re-expansions) to the bounded path,
  or use the trace build (`cmake-build-trace`) to compare `B` vs `T L0=100` event
  streams for `free-cell 1` on the first few thousand events — divergence point tells
  you whether dedup is being defeated.
- Keep using `scripts/m5_collapse.py` for any new instance (it is RAM-safe and
  records depths). Default arms are now `T,B,U`.

## 8. Reproduction

```bash
# Build (release):  ./build.sh --release
# Decisive A/B (same bounded pass, different L) — SHORT timeout for debugging:
python3 scripts/m5_collapse.py --only free-cell_1 --arms B,U --timeout-ms 10000 \
    --out /tmp/ab_big.csv
python3 scripts/m5_collapse.py --only free-cell_1 --arms T --l0 100 \
    --timeout-ms 10000 --out /tmp/ab_l100.csv   # will time out; check states/max_depth

# The cyclic-unsolvable cost=inf win:
python3 scripts/m5_collapse.py --only free-cell_537751 --arms U,B,T \
    --timeout-ms 60000 --out /tmp/cyc.csv

# Raw solver (single run, see JSON fields):
./cmake-build-release/bin/solvitaire --type free-cell --random 1 --force-lru \
    --initial-depth-bound 100 --depth-grow 2 --timeout 10000 --json
```

CSV outputs from the runs in §3 were written to `/tmp/m5_fc1_l0_10k.csv`,
`/tmp/m5_fc1_l0_100.csv`, `/tmp/m5_fc537751.csv` — **ephemeral** (container is
reclaimed); the numbers are transcribed into §3 above and into `progress-log.md`.

## 9. Caveats

- **n = 2 instances.** This is a pilot, not a sweep. Both are free-cell. Other games
  (spanish-patience, two-deck) were *not* measured here.
- The 28,453 "solution depth" is unbounded-DFS order, **not** the optimal solution
  length. We have **not** established the true shortest solution depth for
  `free-cell 1` (the L0=100 pass that would have proven "no solution ≤100" did not
  exhaust in an hour).
- Deep-**unwinnable** instances (the proposal's real target) were **not** tested —
  Ian flagged them as "not the crux now". The negative result above is about
  *winnable* free-cell.
- All runs `--force-lru`; cross-pass reuse only exists for `lru_cache`. Flat/hash
  policies were not exercised.
