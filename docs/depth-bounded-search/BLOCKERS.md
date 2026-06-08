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

**Status: DEFERRED — Ian to revisit (2026-06-08):** "revisit the F1 point as I don't totally
understand it; mark for later work." Not a blocker; tracked for a future walkthrough (best
done alongside building 2b, where it concretely informs the finalisation/reuse code).

**Where this came from.** Authoring the 2d adversarial tests
(`src/test/unit_tests/ghi_cycle_abstract_test.cpp`, the Part-B abstract demonstrator).

**What was observed (verified, not guessed).** I implemented the §4.1 bounded pass faithfully
(DEAD/OPEN(b)/g_min + cross-pass reuse) with the back-edge contribution as a toggle
(finite-ancestor-estimate vs ∞/closed-edge), plus a brute-force reachability oracle, and:
- **Fuzzed ~3.2 M random ≤6-node cyclic graphs**: found **zero** cases where the
  closed-edge-∞ choice *alone* produced a wrong final verdict while the finite (DFSTT3)
  choice was right. Structural reason: any region reachable *through* a back-edge to an
  on-path ancestor `a` is also reachable **from `a` directly**, and `a` is always expanded
  — so in a clean recursive satisficing search the cycle edge contributes no new
  reachability, and truncation-keeps-OPEN propagates correctly regardless of the ∞ vs finite
  choice.
- The genuinely-discriminating false-`unwinnable` is produced by a **"seen ⇒ prune"**
  transposition rule that caches a node as terminal even when a **descendant was truncated**
  (conflating `OPEN(truncated)` with `DEAD`) and persists it across passes. A later, deeper
  pass then prunes at that false-terminal node and exhausts with **no truncation flag** ⇒
  false `unwinnable`. This is the §3.4/§3.3 min-arrival-depth / reuse-inequality violation,
  and it is what the Part-B test now uses (graph `ROOT→D→C→{ROOT, G}`, hand-traced in the
  file). The incompleteness failure (proposal §3.5 naive choice 1, "taint-OPEN ⇒ never
  `unwinnable`") **does** reproduce cleanly and is also asserted.

**Why this matters for 2b (and why it is a finding, not an ambiguity).** It does **not**
change the recommendation to mirror DFSTT3 exactly — the finite back-edge contribution is
still required for *budget/`esti` correctness* (the OPEN budgets it produces feed the
`b ≥ B_now` reuse test), and the formal admissibility proof (Akagi Thm 2) needs it. The
practical takeaway is: **the dominant false-`unwinnable` risk in 2b is finalising a
partially-explored node as terminal (writing DEAD, or trusting a stale OPEN, over a region
that was truncated or reachable-only-via-a-not-yet-finalised cycle), NOT the isolated
back-edge value.** This is exactly the B1 trap ("keying finalisation on the absent cache
iterator silently drops a forced edge's contribution ⇒ parent DEAD over unexplored region")
and the assert-4.4(a) invariant. Test coverage was steered accordingly:
- the **abstract** demonstrator proves teeth on the *seen⇒prune* and *taint* failures (the
  reproducible ones) and shows DFSTT3 stays correct under both back-edge rules;
- the **engine-level** guard (`depth_bound_verdict_test.cpp`, Part A) is the real GHI net:
  its `DISABLED_Stage2_ReuseAcrossPasses_MatchesUnbounded` test, force-run on **today's**
  Stage-1 engine, already yields **42 false-`unwinnable` mismatches** (e.g.
  `-test-spanish-patience` s7/s8, `-test-alpha-star` s4) under naive cross-pass reuse — i.e.
  it will fail loudly if 2b reintroduces that class of bug. After 2b lands, the orchestrator
  should **remove the `DISABLED_` prefix**; it must then go green.

**Severity: INFORMATIONAL (no Ian decision required).** **Status: recorded.** No code rides
on this; it documents the test strategy and reinforces B1.

---

### Status log
- 2026-06-07 — opened B1/B2/B3 from the 2b readiness analysis (night-shift; Ian asleep).
  2b held; 2a + 2d proceed independently.
- 2026-06-08 — 2d adversarial tests authored (Part A engine-level guard +
  Part B abstract demonstrator); recorded finding **F1** (closed-edge-∞ alone does not flip
  the verdict in a clean model; partial-node reuse is the reproducible false-`unwinnable`
  hazard). Tests pass release+debug on today's sound engine. Does not unblock B1.
