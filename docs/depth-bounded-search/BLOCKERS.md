# BLOCKERS — depth-bounded-search

Open questions for **Ian**. Per `night-shift-protocol.md` §1, these are **NOT guessed**
(the red line); autonomous work proceeds on independent items while they stay open.
Each: id · severity · context · options · recommendation · status.

**STATUS: B1/B2/B3 RESOLVED (Ian, 2026-06-08).** Stage 2 **2b is now UNBLOCKED** (deferred to
a focused session — not built yet). Decisions: **B1 = A** (forced uncached edge: pass-through,
`+1`), **B2 = A** (soft-pin `DEAD`), **B3 = `live`-bit per-pass cycle detection kept
cross-pass-clean** (no separate set). **F1 = DEFERRED** (Ian to revisit). Per-section detail
below; grounding: the 2b readiness analysis (proposal §3.3/§3.5/§3.7/§3.8, §4.1; plan §5).

---

## B1 — [Stage 2b · RED LINE · OPEN] Forced uncached (dominance / K+) edge: how does it finalise `DEAD`/`OPEN(b)` and account budget?

**Blocks:** 2b (and therefore 2c). This is the hard blocker.

**Context.** DFSTT3 backs up `verified = min over children of (1 + child_b)` with **unit
edge cost** (proposal §4.1, §3.5). Dominance/auto-foundation and K+ moves are **forced
singletons that count as one ply** (`res.depth++`, `solver.cpp:229`) but are **not
cached** (`solver.cpp:156-159`; proposal §2.1, §6.5). The finalisation hook
`revert_to_last_node_with_children` (`solver.cpp:231/:274`) is keyed on a cache iterator
that dominance nodes **lack** (`solver_node::cache_state == boost::none`). So how a forced
uncached edge threads its status/budget into the nearest **cached** ancestor is undefined
by the docs.

**Options.**
- **A (recommended): pass-through, charge +1.** The forced edge contributes `1 + child_b`
  to its nearest cached ancestor's `verified`; the dominance state stores nothing (stays
  uncached). `DEAD` child ⇒ ancestor `DEAD`; `OPEN(bc)` child ⇒ ancestor `OPEN(1+bc)`.
- **B: cache dominance states with their own finalisation.** Rejected — breaks the
  deliberate "dominance not cached" design + the 2a trace-identity gate; no soundness gain.
- **C: charge 0 for the forced edge** (`verified = child_b`). **Actually SOUND** (correction,
  Ian 2026-06-08): under-counting `b` makes cross-pass reuse strictly *more conservative* —
  it can never falsely prune. Not chosen because A gives **accurate** budgets (stronger
  depth-collapse) and one **uniform ply-metric** consistent with PR1's `res.depth`; C would be
  coherent only if the *whole* metric (cut included) excluded forced moves.

**Recommendation:** **A.** Unit edge cost ⇒ a forced edge must add 1; `B(child)=B(parent)−1`
keeps budget bookkeeping consistent. **Implementation trap to confirm:** the backup must
**skip writing at the uncached node** and fold `1 + child_b` into the nearest cached
ancestor's accumulator. Keying finalisation naively on the (absent) cache iterator would
**silently drop** the contribution ⇒ a parent finalised `DEAD` over an unexplored region
⇒ false `unwinnable` (violates soundness assert 4.4a).

**Severity:** **HIGH (red line).** **Status: RESOLVED → Option A** (Ian, 2026-06-08). K+ stock
moves count as **1 ply** (confirmed; current engine behavior). *2b trap recorded:* fold the
uncached edge's `1 + child_b` upward into the nearest cached ancestor; never key finalisation
on the (absent) cache iterator, or the contribution is silently dropped ⇒ false `unwinnable`.

---

## B2 — [Stage 2b/2c · policy · CONFIRM] Is `DEAD` a **soft** pin (evictable under memory pressure), with `MEM_LIMIT` firing only when **all** entries are `live`?

**Context.** Evicting a `DEAD` entry is **sound** — only loses the collapse/optimisation,
never correctness (proposal §3.7: "Eviction only ever causes re-search, never an unsound
prune"). The `live` bit is the **only** hard (soundness) pin — it prevents ancestor loss →
loops; today the LRU loop throws `MEM_LIMIT` when all entries are live
(`global_cache.cpp:232-240`). Cross-pass persistence means a pass can **start** with the
cache full of pinned `DEAD`; hard-pinning `DEAD` could then throw a **spurious**
`MEM_LIMIT`.

**Options.** **A (recommended):** 3-tier eviction — `live` hard-pinned; `DEAD`
preferred-keep but **evicted before throwing**; `OPEN` normal LRU (shallow/low-`b` first);
`MEM_LIMIT` predicate stays "all remaining are `live`". **B:** hard-pin `DEAD` (risks
spurious `MEM_LIMIT`/`unknown`).

**Recommendation:** **A** — matches plan 2c's "**prefer** evicting shallow/low-budget
`OPEN`". **Not a false-`unwinnable` risk** (only an unknown-rate/RAM tradeoff).
**Severity: MEDIUM. Status: RESOLVED → Option A (soft-pin `DEAD`)** (Ian, 2026-06-08).

---

## B3 — [Stage 2b · design-confirm] Keep `ON_PATH` solely in a **per-pass** `on_path` set; do **not** reuse the persistent LRU `live` bit for cycle detection.

**Context.** Proposal §3.7 + plan 2b(i) specify an explicit per-pass `on_path` hash set of
frontier Zobrist keys (insert on descend / erase on backtrack). The generation stamp
(§3.8) is needed for `ON_PATH` **only if** the marker is **persisted** in cache entries.
A per-pass set, rebuilt empty each pass, is discarded on any mid-pass abort ⇒ **no
cross-pass staleness ⇒ no generation stamp needed** for `ON_PATH`.

**Options.** **A (recommended):** `ON_PATH` lives only in the per-pass set; persistent
cache stores no cycle/on-path bit. **B:** persistent `live` bit for cycles + generation
stamp. **C:** persistent bit + clear-on-abort (fragile — many abort paths:
timeout/SIGINT/`MEM_LIMIT` throw).

**Status: RESOLVED (Ian, 2026-06-08) — refined.** Cycle detection is **intra-pass only** (a
new pass is a fresh search from root; only the cache's *reuse* info crosses passes), so **no
separate per-pass set is needed**. Keep the existing `live` bit for cycle detection and simply
guarantee **no stale `live` bit survives into a new pass** (assert 4.4d). Mechanism = 2b
implementer's choice, both sound: (i) **zero** the live bits at pass exit (clear the abandoned
frontier), or (ii) a **generation stamp** on the live mark. Cache nodes (`dead`/`b`/`g_min`)
persist for reuse; only the on-path/`live` state resets each pass. **Severity: MEDIUM
(red-line-adjacent if a stale `live` bit leaks).**

---

## F1 — [Stage 2d · FINDING · informational, not a blocker] In a pure recursive reachability model, the "closed-edge / contributes ∞" back-edge rule (proposal §3.5 naive choice 2) does NOT, by itself, flip the final winnable/unwinnable verdict — the false-`unwinnable` hazard is reproduced by the **partial-node-reuse** rule instead.

**Status: WALKED THROUGH + RESOLVED (2026-06-08, Ian asked to do it now, while building 2b).**
The 2b engine made F1 concrete — see the plain-language walkthrough below. **Bottom line: the
finite DFSTT3 back-edge contribution is the safe, proven choice we ship; the real
false-`unwinnable` hazard is partial-node reuse (writing DEAD over a truncated / not-yet-finalised
region), which the B1 fold-through + finalise-only-on-`expanded` + the teeth tests all guard.**

### F1 in plain language (the walkthrough Ian asked for)

**The puzzle.** Proposal §3.5 lists two ways to handle a back-edge `s → a` to an on-path
ancestor `a`:
- *naive 1 "taint-OPEN"*: once any cycle is seen, never finalise `DEAD` ⇒ **sound but
  incomplete** (a reachable cycle blocks every unwinnability proof forever).
- *naive 2 "closed-edge / contributes ∞"*: the back-edge contributes `∞` ⇒ the proposal calls
  this **complete but UNSOUND** (it could let `s` be finalised `DEAD` while `s` can still reach,
  through the cycle, a region that later truncates).

F1 is the finding that **naive 2 does not, by itself, flip the final verdict in a satisficing
reachability search** — which seemed to contradict "unsound". The walkthrough reconciles it.

**Why naive-2 looks harmless (the structural argument).** The cycle target `a` is an
**ancestor of `s` that is itself always expanded** on the current path. Anything `s` can reach
*through* the back-edge `s → a` is reachable **from `a` directly**. So the cycle edge adds no
new reachability that the search wouldn't already see by expanding `a`. Concretely: `s` is
finalised `DEAD` only if **all of `s`'s own direct children were `DEAD`** (no truncation among
them — a truncated child would fold `1` up and force `s` OPEN). `s`'s direct subtree is therefore
genuinely exhausted; the *only* thing the `∞` choice "hides" is the `s → a` edge, which reaches
nothing new. So pruning a `DEAD`-via-∞ `s` in a later pass loses nothing, and if `a`'s region
ever holds a goal, expanding `a` directly finds it (SOLVED) before `s`'s prune could matter. The
~3.2 M-graph fuzz (2d) saw **zero** verdict flips from the ∞ choice for exactly this reason.

**So why does 2b use the FINITE contribution anyway?** Three reasons, in order of importance:
1. **It is unconditionally safe; ∞ relies on the subtle argument above.** A finite back-edge
   makes `verified = 1 + a.esti` **finite**, so `s` is finalised **OPEN, never DEAD**. OPEN can
   only ever cause *re-search* (sound), never an unsound prune. We do **not** want soundness to
   hinge on the "cycle target is an always-expanded ancestor" reasoning — the finite rule is
   safe even if that reasoning has an edge case we missed.
2. **Accurate budgets.** The reuse inequality `prune iff b ≥ B_now` and the cross-pass collapse
   need *real* `OPEN(b)` budgets; `∞` would throw that information away.
3. **It is the proven rule.** DFSTT3 (Akagi Thm 2) is admissible + complete with the finite
   contribution; we inherit the proof rather than re-deriving per case.

**The engine confirmed this concretely.** A first, over-strict debug assert
(`root.verified == ∞ ⟺ ¬any_truncation`) **aborted on cyclic games** — because a fully-exhausted
node in a cyclic region backs up to a **finite OPEN esti even with `any_truncation == false`**
(the finite cycle contribution at work). That is correct and intended: the verdict rests on
**`any_truncation`**, not on the root being `DEAD`. (Matches `ghi_cycle_abstract_test.cpp`'s
`id_dfstt3`, which returns UNWINNABLE iff `!any_truncation`.) The assert was removed; soundness
asserts 4.4(b) (OPEN-prune only when `b ≥ B_now`) and 4.4(d) (no stale live bit across passes)
remain and pass.

**The REAL hazard (what actually produces a false `unwinnable`).** Not the back-edge value, but
**partial-node reuse**: caching a node as terminal/`DEAD` when a descendant was **truncated** or
a contribution was **silently dropped** (e.g. the B1 trap — keying finalisation on a forced
uncached edge's absent cache iterator), then persisting it so a later, deeper pass prunes there
and exhausts with no truncation flag ⇒ false `unwinnable`. 2b defends this with: the `verified`
fold that runs for **every** popped node incl. uncached dominance/K+ (B1=A); `finalise_node`
writing only `expanded` nodes (never a hit/cycle/truncated node, never un-living an ancestor);
truncated leaf ⇒ `b = 0` ⇒ ancestors forced OPEN. The teeth tests target exactly this class:
`LruReuseAcrossPasses_MatchesUnbounded` (engine-level, enabled) + the abstract `seen⇒prune` /
`taint` demonstrators — both shown to FAIL on the naive rule and PASS on DFSTT3.

**Where this came from.** Authoring the 2d adversarial tests
(`src/test/unit_tests/ghi_cycle_abstract_test.cpp`, the Part-B abstract demonstrator).

**What was observed (verified, not guessed).** A faithful §4.1 bounded pass with the back-edge
contribution as a toggle (finite vs ∞), plus a brute-force reachability oracle:
- **Fuzzed ~3.2 M random ≤6-node cyclic graphs**: **zero** cases where the closed-edge-∞ choice
  *alone* produced a wrong final verdict while the finite (DFSTT3) choice was right (the
  structural reason above).
- The genuinely-discriminating false-`unwinnable` is the **"seen ⇒ prune"** partial-node-reuse
  rule (graph `ROOT→D→C→{ROOT, G}`, hand-traced in the file). The incompleteness failure
  (taint-OPEN) also reproduces cleanly and is asserted.

**Severity: INFORMATIONAL.** **Status: resolved/understood.** No code rides on the ∞-vs-finite
verdict-flip question; 2b ships the finite rule for the safety/accuracy/proof reasons above, and
the dominant hazard (partial-node reuse) is guarded by B1 + the teeth tests.

---

## B4 — [Stage 2b · scope · RESOLVED] LRU-first vs the flat-reuse teeth test contradiction

**Context.** HANDOFF line 31 says implement 2b "LRU first" (Q6; flat/hash/predecessor
deferred). HANDOFF line 35 says the teeth test
`DISABLED_Stage2_ReuseAcrossPasses_MatchesUnbounded` "must go green" — but that test drives
`id_flat_reuse<FlatPolicy>` (cross-pass reuse on the **flat** cache). An LRU-only 2b leaves
flat cross-pass reuse on the naive "in cache ⇒ prune" rule, so that specific flat test stays
a false-`unwinnable` generator and cannot go green. The flat path also needs strictly more
work (flat `insert_t` no-ops on hit — no update path — and has no `live` bit; proposal §6.4).

**Status: RESOLVED → LRU-only; retarget the teeth test (Ian, 2026-06-08, lead present).**
- 2b implements cross-pass reuse for **LRUPolicy only**. The product ID loop
  (`solve_game_impl`) keeps the cache across passes **only for LRU**; flat/hash/predecessor
  stay fresh-cache-per-pass (sound Stage-1 behaviour) under a bound.
- Add an `id_lru_reuse` driver + an **ENABLED** LRU cross-pass-reuse teeth test that must go
  green AND must demonstrably fail on a naive LRU reuse (real teeth, shown during verify).
- Keep the flat-reuse test `DISABLED_`, retitled to track the **deferred** flat 2b.
- All 2b machinery is gated on `!Policy::computes_hash && depth_bound`, so the L=∞ legacy
  path is byte-identical (identity gate unaffected). `dead`/`b`/`g_min` are written **only**
  under a bound, so 2c's DEAD-pin eviction cannot perturb unbounded eviction (identity-safe).

### Status log
- 2026-06-07 — opened B1/B2/B3 from the 2b readiness analysis (night-shift; Ian asleep).
  2b held; 2a + 2d proceed independently.
- 2026-06-08 — 2d adversarial tests authored (Part A engine-level guard +
  Part B abstract demonstrator); recorded finding **F1** (closed-edge-∞ alone does not flip
  the verdict in a clean model; partial-node reuse is the reproducible false-`unwinnable`
  hazard). Tests pass release+debug on today's sound engine. Does not unblock B1.
