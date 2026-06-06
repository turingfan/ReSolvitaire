# Open Questions for the Author — RESOLVED 2026-06-06

Consolidated from [`proposal.md`](proposal.md). These shaped the design before
implementation. **All seven are now answered by the author (Ian Gent,
2026-06-06);** each carries a `Decision` block below. The decisions and their
combined effect on the staging are summarised in **proposal.md §1.5**.

| # | Topic | Decision (short) |
|---|---|---|
| Q1 | Primary objective | **(d)** faster *unwinnable* proofs via depth-collapse; **(a)** shallow wins secondary |
| Q2 | Incompleteness | **A complete mode is mandatory** |
| Q3 | Reuse encoding | Absolute budget `b` (explanation given); generations not needed |
| Q4 | Metadata layout | **Option C — the `DEAD` bit** (definitive, monotone) + side table for `OPEN` |
| Q5 | Initial bound `L0` | **`≈ 1000`** to start, sweep upward (≤ `10⁶` fine); geometric `×2` |
| Q6 | Cache scope | **Start with `LRUPolicy`** (ease of implementation) |
| Q7 | Do flat games cycle? | **Yes** — reinforces LRU-first |

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

> **Decision (author, 2026-06-06):** Big goal is **(d)**, and to a lesser extent
> **(a)**. *Consequence:* the payoff lives in **Stage 2** (cross-pass `DEAD`
> retention), not Stage 1; Stage 0's depth-collapse probe (proposal §5) becomes
> the **go/no-go gate**. See §1.5.

### Q2 — Is any incompleteness acceptable?

The recommended scheme is **complete in the limit** (`L → ∞` decides every
instance). The optional "half-depth" re-expansion filter reduces re-search churn
but is **incomplete** (can strand hard instances forever).

> **Decision (author, 2026-06-06):** **There must be a mode where incompleteness
> is unacceptable.** *Consequence:* ship the sound `b < B_now` scheme (complete
> in the limit) as the default/required mode; the lossy half-depth filter (§3.6)
> stays strictly opt-in and measured. Other (lossy) modes may co-exist, but the
> complete mode must always be available.

### Q3 — Cross-pass reuse encoding (Stage 2)

Two equivalent encodings of "trust an `OPEN` node only if it was searched with at
least as much budget":

- **(a) Absolute budget `b`** stored per entry; rule `prune iff b ≥ B_now`. No
  generation counter needed. *Recommended.*
- **(b) Generation + depth** (the "cleanse" you described); rule `prune iff same
  generation and recorded depth ≤ current depth`; bump generation each pass.

> **Author asked for a fuller explanation of why absolute `b` needs no generation
> counter.** Given in chat and now in proposal §3.3 (worked example). In short:
> `b = L − d` is an *absolute* move count, so the comparison `b ≥ B_now` is
> correct no matter which pass wrote the entry — a smaller-`L` pass simply writes
> a smaller `b`, which automatically fails the next test and triggers re-open.
> The generation scheme re-derives the same `b` implicitly and only saves bits
> (no real saving here, §6.3-D). **Absolute `b` remains the recommendation**, and
> it pairs cleanly with the Q4 dead bit (`DEAD` is budget-independent → carries no
> number; only the `OPEN` minority stores `(g_min, b)`).

### Q4 — Per-entry metadata layout (Stage 2)

The deep games reach depth `1.9 × 10⁸`, so the existing 16-bit (flat) / 8-bit
(predecessor) depth fields cannot hold `g_min`/`b`. Options (proposal §6.3):

- **(A)** parallel `{g_min,b}` array beside the 64 B key clusters;
- **(B)** widen `compact_state` (breaks the 64 B cluster invariant);
- **(C)** a `DEAD` bit in the key entry + a small side table for the `OPEN`
  minority;
- **(D)** generation+depth packing.

> **Decision (author, 2026-06-06):** *"Don't know, agree there are tradeoffs to
> be explored. Choose a sensible one and then investigate if we have it working.
> Though I like the dead bit at least as that is definitive and stays true once
> set."* → **Option C (the `DEAD` bit)** chosen, for its decisive, monotone
> property (set once, never cleared). **Investigation (code read on this branch):**
> *LRU* — the mutation substrate already works: `cached_game_state` has a mutable
> `bool live` mutated via `lru_cache::set_non_live` → `cache.modify(...)`
> (`global_cache.cpp:273`), so adding `status`/`b`/`g_min` + a `set_dead` is a
> direct extension (the dead bit is near-free here). *Flat* — the bit has a home
> (a spare bit of `compact_state` byte 0 or the depth bytes, both already excluded
> from `matches()`), **but** `insert_t` no-ops on a hit (`generic_flat_cache.h:82`)
> so there is no update-on-hit path to flip `OPEN→DEAD` yet; flat needs the new
> `probe_and_update` upsert (§6.4) first. See §1.5.

### Q5 — Initial bound `L0` and growth schedule

- `L0`: per game (from Stage-0 depth distributions) vs. a single global default?
- growth: geometric `×2` vs. another schedule?
- `L_max` / give-up: tie to the existing `--timeout`, or a separate cap?

> **Decision (author, 2026-06-06):** Not aiming at true optimality (that is
> against the deep-search problem), so **start `L0 ≈ 1000`** to get going, and
> **experiment** upward — *"even 1 million isn't crazy given RAM of modern
> machines."* Geometric **`×2`** growth. *Consequence:* a small `L0` maximises
> depth-collapse pressure but means ~18 doublings to reach 256 M, viable **only**
> with Stage-2 reuse (a fresh-cache pass would re-search ~18×). `L_max`/give-up:
> tie to existing `--timeout` for now (open to a separate cap later). See §1.5/§6.5.

### Q6 — Scope of caches to support

Start with which policy? `FlatPolicy` (most single-deck games) vs. `LRUPolicy`
(easiest to extend — mutable entries already exist). `HashOnlyPolicy` cannot
store per-entry budget without growing its 16 B clusters.

> **Decision (author, 2026-06-06):** *"Ease of implementation, so that might
> suggest LRU. Again can be optimised later."* → **Start with `LRUPolicy`.**
> Flat / hash-only / predecessor / multiplicity deferred. The LRU `live`-bit
> mutation path (Q4 investigation) is exactly the substrate Stage 2 needs.

### Q7 — Empirical: do flat-cache games actually cycle?

Cycle/GHI handling (proposal §3.5) is the main correctness cost, but it only
matters if the flat-cache games contain reachable cycles.

> **Decision/answer (author, 2026-06-06):** **Yes, flat-cache games do cycle** —
> *"another reason to start with LRU."* *Consequence:* the flat path will need
> full GHI handling (§3.5), so deferring it behind the LRU implementation (Q6) is
> the right order. Stage 0 can still quantify *how much* they cycle to size the
> work.
