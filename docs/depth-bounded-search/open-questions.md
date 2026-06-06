# Open Questions for the Author

Consolidated from [`proposal.md`](proposal.md). These shape the design before
implementation; defaults are given where I have a recommendation.

---

### Q1 — Primary objective

What is the main thing this should buy us? The stages weight differently:

- **(a) Shallow-win-finding** on the *winnable* deep tail (the "190 vs 190 M"
  case). Strongest, clearest payoff. Favours getting Stage 1 right.
- **(b) Peak-RAM reduction** on the deep outliers (capping trail + pinned
  ancestors at `O(L)`). Also delivered largely by Stage 1.
- **(c) An anytime / "probably unknown" mode** that gives a confident answer
  quickly and only deepens hard instances.

- **(d) Faster *unwinnable* proofs on the deep tail** — via the depth-collapse
  hypothesis (proposal §1.3): persistent cross-pass `DEAD` reuse can prove the
  same `unwinnable` result at a far shallower maximum depth than unbounded DFS,
  because the 27-M depths are a search-order artefact, not intrinsic.

These are compatible; I want to know which to optimise for if they conflict. Note
(d) is the corrected understanding — depth bounding **with persistent reuse** can
help deep unwinnable proofs (it is the central hypothesis to test), not just the
winnable tail. The constraint-based route (Dang et al. 2025) remains a
complementary tool for *locally*-caused unwinnability.

### Q2 — Is any incompleteness acceptable?

The recommended scheme is **complete in the limit** (`L → ∞` decides every
instance). The optional "half-depth" re-expansion filter reduces re-search churn
but is **incomplete** (can strand hard instances forever). Solvitaire already
tolerates a measured fraction of "unknown" instances within its Wilson ±0.1% CI.

- **Default:** complete scheme only; half-depth off.
- **Alternative:** allow the lossy filter as an opt-in mode, with mandatory
  unknown-count + CI-widening reporting.

Which do you want as the shipped default?

### Q3 — Cross-pass reuse encoding (Stage 2)

Two equivalent encodings of "trust an `OPEN` node only if it was searched with at
least as much budget":

- **(a) Absolute budget `b`** stored per entry; rule `prune iff b ≥ B_now`. No
  generation counter needed. *Recommended.*
- **(b) Generation + depth** (the "cleanse" you described); rule `prune iff same
  generation and recorded depth ≤ current depth`; bump generation each pass.

Both are sound. (a) is simpler to reason about; (b) matches your mental model and
makes the inter-pass "cleanse" a one-line generation bump. Preference?

### Q4 — Per-entry metadata layout (Stage 2)

The deep games reach depth `1.9 × 10⁸`, so the existing 16-bit (flat) / 8-bit
(predecessor) depth fields cannot hold `g_min`/`b`. Options (proposal §6.3):

- **(A)** parallel `{g_min,b}` array beside the 64 B key clusters *(recommended)*;
- **(B)** widen `compact_state` (breaks the 64 B cluster invariant);
- **(C)** a `DEAD` bit in the key entry + a small side table for the `OPEN`
  minority *(most memory-efficient)*;
- **(D)** generation+depth packing.

Any preference, or shall I prototype (A) and (C) and benchmark?

### Q5 — Initial bound `L0` and growth schedule

- `L0`: per game (from Stage-0 depth distributions) vs. a single global default?
- growth: geometric `×2` (recommended) vs. another schedule?
- `L_max` / give-up: tie to the existing `--timeout`, or a separate cap?

### Q6 — Scope of caches to support

Start with which policy? `FlatPolicy` (most single-deck games) is the obvious
first target; `LRUPolicy` is the easiest to extend (mutable entries already).
`HashOnlyPolicy` cannot store per-entry budget without growing its 16 B clusters.
Should Stage 2 target flat + LRU first and defer hash-only / predecessor /
multiplicity?

### Q7 — Empirical: do flat-cache games actually cycle?

Cycle/GHI handling (proposal §3.5) is the main correctness cost, but it only
matters if the flat-cache games contain reachable cycles (the K+ representation
and `creates_immediate_loop()` suppress many). Stage 0 can measure this directly.
If flat-cache games are effectively acyclic, Stage 2 simplifies considerably. Do
you already know the answer for, say, Klondike / FreeCell / Beleaguered Castle?
