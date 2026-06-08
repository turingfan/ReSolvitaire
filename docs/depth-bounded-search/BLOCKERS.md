# BLOCKERS — depth-bounded-search

Open questions for **Ian**. Per `night-shift-protocol.md` §1, these are **NOT guessed**
(the red line); autonomous work proceeds on independent items while they stay open.
Each: id · severity · context · options · recommendation · status.

**Bottom line:** Stage 2 **2b** (cross-pass reuse) is **held** pending **B1** (a genuine
red-line decision) plus confirmation of **B2/B3** (recommended defaults below). Nothing
in 2b is implemented autonomously. Independent safe work (2a cache-format, 2d adversarial
tests) proceeds meanwhile. Grounding for all three: the 2b readiness analysis (proposal
§3.3/§3.5/§3.7/§3.8, §4.1; plan §5 Stage 2; code cited inline).

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
- **C: charge 0 for the forced edge** (`verified = child_b`). **UNSOUND** — over-states
  `b` ⇒ premature cross-pass reuse-prune ⇒ **FALSE `unwinnable`**; also lets forced chains
  descend past `L` uncapped.

**Recommendation:** **A.** Unit edge cost ⇒ a forced edge must add 1; `B(child)=B(parent)−1`
keeps budget bookkeeping consistent. **Implementation trap to confirm:** the backup must
**skip writing at the uncached node** and fold `1 + child_b` into the nearest cached
ancestor's accumulator. Keying finalisation naively on the (absent) cache iterator would
**silently drop** the contribution ⇒ a parent finalised `DEAD` over an unexplored region
⇒ false `unwinnable` (violates soundness assert 4.4a).

**Severity:** **HIGH (red line).** **Status: OPEN — needs your decision.**

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
**Severity: MEDIUM. Status: CONFIRM** (default A; no code rides on it until 2b unblocks).

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

**Recommendation:** **A** — satisfies assert 4.4d ("no `ON_PATH` survives a completed
pass") by construction. **Load-bearing condition:** the `live` bit may still pin ancestors
against eviction, but **cycle/on-path detection must be the single per-pass set** — do not
let `ON_PATH` semantics leak into a persisted bit (that silently reintroduces staleness ⇒
phantom-cycle pruning ⇒ possible false `unwinnable`). **Severity: MEDIUM (red-line-adjacent
if violated). Status: CONFIRM** (default A).

---

---

## F1 — [Stage 2d · FINDING · informational, not a blocker] In a pure recursive reachability model, the "closed-edge / contributes ∞" back-edge rule (proposal §3.5 naive choice 2) does NOT, by itself, flip the final winnable/unwinnable verdict — the false-`unwinnable` hazard is reproduced by the **partial-node-reuse** rule instead.

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
