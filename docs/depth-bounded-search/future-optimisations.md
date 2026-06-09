# Future optimisations — depth-bounded-search (search-saving, NOT correctness)

Tracked opportunities that would **save search** but are deliberately deferred for safety /
staging. None is a correctness blocker; each must ship with its own teeth tests so it cannot
introduce a false `unwinnable`. Newest first.

---

## O1 — Collapse cyclic-dead regions to DEAD via the +inf back-edge — **DONE (now the default)**

**Status: IMPLEMENTED (Ian, 2026-06-08).** Originally logged here as a deferred optimisation;
Ian then reconsidered and directed that we adopt it now: a back-edge to an on-path ancestor
contributes **+inf** (closed edge) by default, so a cyclic region with no truncation/goal below
it finalises **DEAD** and collapses across passes. Kept behind `--finite-cycle-backedge` (forces
the finite DFSTT3 estimate) for A/B comparison. Sound by the prefix-reachability argument
(the cycle target is an always-expanded ancestor reachable independently of the node), verified
by `GhiCycleAbstract.ClosedEdgeCycleDeadDoesNotHideGoalBehindCycle_BothRules` + the engine teeth
test + the ~3.2 M-graph 2d fuzz. See `BLOCKERS.md` F1 for the full reasoning. *(Original
deferred-optimisation text retained below for history.)*

**Severity: optimisation (no soundness impact) — now the default behaviour.**

**What we do today (the cautious, proven-safe choice).** 2b mirrors DFSTT3 exactly: a back-edge to
an ON_PATH ancestor `a` contributes `a`'s **finite** provisional estimate, never `∞`. Consequence
(see `BLOCKERS.md` F1): a node `s` inside a cyclic region that is in fact genuinely **DEAD** (no
goal, and no truncation reachable even through the cycle) is finalised **OPEN(finite)** rather than
DEAD, purely because one of its descendants was a back-edge to an unfinalised ancestor. Stored OPEN,
`s` is **re-searched** on later, deeper passes (its `b` is below the larger `B_now`), instead of
being pruned outright as a persistent DEAD would be. That re-search is the cost.

**Why a promotion-to-DEAD would be safe (the F1 structural argument).** Anything reachable *through*
the back-edge `s → a` is reachable **from `a` directly**, and `a` is always expanded. Once `a`
itself is finalised **DEAD** (its whole subtree exhausted with no truncation and no goal), the cycle
`s → a` demonstrably reaches nothing unexplored, so `s` (and the rest of that strongly-connected
region) can be **upgraded OPEN → DEAD** without risking a false `unwinnable`. The 2d fuzz (~3.2 M
cyclic graphs) found the closed-edge-∞/DEAD choice never flips a satisficing-reachability verdict —
exactly this region.

**Sketch of a safe implementation (for whoever picks this up).**
- After a node `a` is finalised DEAD, the OPEN nodes whose only finite contribution came from a
  back-edge **to `a`** (or to a node now DEAD) are candidates for promotion to DEAD.
- Cleanest formulation: run the DFSTT3 backup **to a fixpoint** over the strongly-connected region
  (or detect SCC closure) so a fully-resolved cycle collapses to DEAD in one pass, instead of
  leaking finite OPEN budgets that force re-search. (Akagi's DFSTT variants discuss exactly this
  fixpoint behaviour.)
- **Mandatory:** ship it behind the existing teeth tests
  (`DepthBoundVerdictTest.LruReuseAcrossPasses_MatchesUnbounded` + the abstract `ghi_cycle_abstract`
  battery) and the differential harness; a promotion that ever turns a truly-OPEN (truncation-below)
  node DEAD is a false-`unwinnable` and must be caught. The invariant to preserve: **promote to DEAD
  only when every truncation/back-edge below the region has itself resolved to DEAD** — never while
  any descendant is still OPEN-because-truncated.

**Expected payoff.** Removes repeated cross-pass re-search of resolved cyclic regions — the same
"depth collapse" mechanism 2b/2c already exploit for acyclic DEAD, extended to cyclic-but-dead
regions. Largest on stock/cell/reserve games (many reversible moves ⇒ many cycles).

---

## O2 — Cross-pass reuse for suit-symmetry games / the `--force-lru` ≠ default discrepancy

**Status: OPEN — informational (Ian accepts the current cautious approach, 2026-06-08).**
**Severity: optimisation / completeness-of-streamliner, not the trusted red line.**

The `both`/suit-symmetry streamliner is lossy (unbounded `--streamliners both` itself reports
`unsolvable` on winnable deals, e.g. `klondike-deal-8_316`, `klondike-deal-11-noworryback_903394`
under `--force-lru`; `smart` exists precisely to retry with `none`). Cross-pass reuse interacts with
suit-canonicalisation in ways that change which reduced states are explored (2b ID under `both` was
seen to find wins the unbounded `both` pass missed — the *safe* direction). A future pass could
(a) characterise suit-symmetry + cross-pass reuse properly and (b) decide whether to enable the
collapse for suit-symmetry games or keep them on a fresh cache. Trusted unwinnable always uses the
complete mode (`none`/`smart`), where 2b is verdict-identical to unbounded (L2/L3 same-config
differential: 0 mismatch), so this is purely a search-saving / streamliner-completeness question.
