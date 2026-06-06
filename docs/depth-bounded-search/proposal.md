# Depth-Bounded, Cache-Reusing Iterative Deepening for ReSolvitaire

**Date:** 2026-06-06
**Status:** Design proposal — for author review (no code yet)
**Authors:** Ian Gent & AI assistant
**Branch:** `claude/ecstatic-hopper-tpykG`

**Reading order:** §1 (why) → §2 (what breaks) → §3 (theory) → §4 (algorithm)
→ §5 (staging) → §6 (code touch-points). §7–§9 cover testing, risks, and
questions for the author. If you only read three sections, read §3.5 (cycles —
the hard part), §4, and §5.

---

## 1. Motivation and goals

### 1.1 The problem

Solvitaire is an exhaustive depth-first solver. Its great strength — the reason a
reported `unwinnable` can be trusted — is that it explores *every* reachable
position before concluding a game cannot be won (JAIR §5.1). But unbounded DFS
has two documented pathologies, both called out by the paper itself:

> "We would mention two [tradeoffs] in particular. First, we do not even
> approximate getting the shortest possible solution: in the example above there
> may well be a solution at depth 190 instead of 190 million. Second, search can
> spend a long time in an area where there is no solution after an early
> incorrect choice, leading to very long search times for instances that might be
> easily solvable by more flexible methods. We did consider the use of iterative
> deepening to avoid the first problem and also possibly the second. However,
> preliminary experiments suggested that the overhead of iterative deepening did
> not pay off for our primary goal of determining winnability." — Blake & Gent,
> JAIR 85 (2026), §5.1

The concrete numbers from the same section:

- *Beleaguered Castle*, one instance **solved at maximum search depth > 190
  million** (460 M nodes, 1,270 s). A legal win almost certainly existed orders
  of magnitude shallower.
- Another instance **proved unwinnable at maximum depth > 27 million** (> 1
  billion nodes, 2,131 s).

And the memory pathology (JAIR §5.2):

> "[We] never discard any ancestor of the current state, as otherwise loops can
> occur. If the transposition table is entirely full and all states in it are
> ancestors, then we give up on search and report that a memory-out has
> occurred. In extreme cases very large amounts of RAM are necessary, up to
> hundreds of gigabytes ... the worst case with the largest cache was a
> requirement of 67 GB."

Peak RAM is dominated by `O(trail length) + O(pinned ancestors) + O(TT working
set)`. The first two are `O(depth)`. A 27-/190-million-deep path therefore pins
an enormous trail **and** an enormous, un-evictable ancestor set. A depth bound
`L` caps both at `O(L)`.

### 1.2 What we want

Add an **iterative-deepening outer loop with a reusable transposition table** so
that:

1. **Shallow wins are found fast.** If a deal is winnable at depth 190, we find
   it long before exploring to depth 190 million.
2. **The maximum search depth collapses for *unwinnable* instances too** (the
   central hypothesis, §1.3): persistent cross-pass `DEAD` reuse lets a later,
   deeper pass be cut at the shallow `DEAD` nodes a shorter pass already proved,
   so the same `unwinnable` verdict is reached far shallower than the DFS-snake
   length.
3. **The unwinnability guarantee is preserved.** A reported `unwinnable` is still
   a complete-exhaustion certificate, *never* an artefact of the bound.
4. **Cross-pass work is reused.** Re-searching from scratch at each bound is the
   "overhead [that] did not pay off". We keep what is provably bound-independent
   (dead subtrees) and re-do only what the larger budget might change.
5. **Peak RAM on the deep tail is reduced**, not merely relocated, by capping
   trail/ancestor depth (the collapse in (2) shrinks it further: few ancestors ⇒
   the cache can evict normally instead of hitting "all entries are ancestors").

### 1.3 The central hypothesis: iterative deepening can *collapse* the search depth

The most important effect — and the real point of the idea — is that
depth-bounded ID **with a persistent, reused cache** can dramatically reduce the
*maximum search depth* reached, for **winnable and unwinnable instances alike**.
The 27-/190-million depths are largely an **artefact of depth-first exploration
order**, not an intrinsic property of the state space.

Why the unbounded search goes so deep — using JAIR §5.1's own diagnosis of the
27-M unwinnable instance: the tree is "very tall but thin", averaging *under 40
nodes per depth*, because "almost all configurations may be achievable by
continuing to move rather than backtracking to the root, with transposition
tables preventing states being revisited." That is, DFS commits to a line and
snakes downward; each state is first encountered *deep along the snake* and cached
there, so its much shorter alternative paths later hit the cache and are skipped.
The deep snake length becomes the max depth even though most of those states are
reachable far shallower.

**A depth bound inverts the discovery order.** In the pass at bound `L`, a state
that sits at snake-depth 1.5 M *cannot* be reached via the deep path when
`L < 1.5 M` — so it is discovered (if at all) via its **shortest** path. Once its
subtree is fully exhausted at that shallow depth it is marked `DEAD`, and
**because the cache persists across passes**, every later, deeper pass that snakes
back down to it is **cut off at the shallow `DEAD` node**. The author's own
example: if the `L = 1 M` pass proves a region dead, then in the `L = 2 M` pass
"every new state we consider beyond depth ~1.1 M is identical to ones we already
proved unwinnable in the 1 M search" — so the 2 M pass never actually descends to
2 M.

The maximum depth the scheme needs is therefore governed by the **shortest-path
structure** (eccentricity) of the reachable space, not by the DFS-snake length.
For a human patience game it is "unlikely that games with such deep searches are
fundamentally essential", so the collapse should usually be large. This is a
**hypothesis to test, not a theorem** (see §1.4), but if it holds it attacks the
deep-*unwinnable* tail *and* the RAM blow-up, not merely the winnable tail.

**Two corollaries that reshape the design:**

1. **The collapse requires cross-pass persistence with `DEAD` retention
   (Stage 2), not bounding alone (Stage 1).** A *fresh*-cache pass can re-snake
   to the full depth before any shallow `DEAD` is rediscovered within that pass;
   only a *persisted* `DEAD` set forces the early cut. So Stage 2 is the heart of
   the idea, and `DEAD` entries must be *pinned* against eviction (§3.7).
2. **This differs materially from the ID the paper tried.** Vanilla IDA\*/ID
   keeps linear space precisely by *not* persisting the table; it re-searches
   every pass and gets the overhead with *none* of the collapse. The negative
   "ID didn't pay off" result almost certainly used that vanilla form. ID + a
   *persistent, terminal-retaining* cache is the novel combination (the prior
   review notes it is "not packaged exactly this way in the literature").

### 1.4 Honest caveats

- **The collapse is not guaranteed.** If a game genuinely requires deep lines (a
  state reachable only via a long path, no shortcut), the bound buys little and
  `L` may have to approach the unbounded depth. The downside is bounded, though:
  with geometric growth the final `L` overshoots the true requirement by at most
  ≈2×, and cache reuse limits the re-search cost — so the bad case is "no worse
  than today, plus some overhead", while the good case is a large win. As the
  author puts it: *"we might try it and it doesn't work, but that's ok."*
- **Depth bounding does not change the *set* of distinct states**, only the depths
  at which they are explored. The transposition table still ends up holding
  roughly the same number of distinct states; the RAM win comes specifically from
  collapsing the `O(depth)` trail + pinned-ancestor cost (§3.7), which is exactly
  what caused the "all entries are ancestors → memory-out" failure.
- **Constraint-based unwinnability (Dang et al., CP 2025) remains complementary.**
  It proves unwinnability by a different route (relaxed-game UNSAT) and is the
  better tool when the reason is a small *local* one; the depth-collapse attacks
  the *search-order* cause of deep refutations. They are not mutually exclusive.
- **Lossy variants can break completeness.** A "double until it fits" schedule
  with the *half-depth* re-expansion filter (§3.6) can strand instances. The
  default recommended scheme is **complete** in the limit `L → ∞`; lossy variants
  are opt-in and measured.

---

## 2. What the current code does, and which invariants break under a bound

References are to files read on this branch.

### 2.1 The search engine (`src/main/solver/solver.cpp`)

The DFS is **iterative with an explicit stack** (`frontier`, a
`std::vector<solver_node>`). `frontier` *is* the current root-to-node path (the
trail); `current_node` points at its tail. Per node it holds the `move` that
created it and the not-yet-tried `child_moves`. The main loop (`dfs()`):

1. timeout / SIGINT check;
2. if a **dominance move** exists, push it as the sole child and loop (dominance
   states are **not** cached);
3. else **insert** the current state into the cache:
   - **new state** → generate `get_legal_moves()`; if empty, backtrack;
     otherwise store the children;
   - **already present** → `revert_to_last_node_with_children()` (backtrack);
4. if not exhausted, `set_to_child()` + `make_move()`, `res.depth++`.

Termination: `state.is_solved()` → `SOLVED`; or
`revert_to_last_node_with_children()` returns `true` (backtracked past the root
with nothing left) → `UNSOLVABLE`.

Key facts:

- **No depth bound exists.** `res.depth` is tracked (and `res.max_depth`), but
  nothing caps it.
- **"In cache → backtrack" is the only prune.** This conflates two situations:
  (a) a transposition to an **ancestor** still being explored (cycle avoidance),
  and (b) a transposition to a node whose **entire subtree is already explored**
  (dedup). Both are sound *because the search is unbounded*.
- **`UNSOLVABLE` = global exhaustion**, not a per-node property. It means "the
  frontier emptied with no goal found", and is trusted because, with no bound,
  every cached node's subtree was (or will be) fully explored.

### 2.2 The caches

`game_state_impl<Policy>` and `solver_impl<Policy>` are templated on a cache
policy (`cache_policy.h`); five policies exist. Two cache families matter:

**Flat caches** (`generic_flat_cache<P>`, `generic_flat_cache_policies.h`) —
open-addressed, fixed-capacity, two slots per cluster, **depth-preferred TwoBig1
replacement**:

- `compact_state` (32 B) layout (`compact_state.h`): byte 0 = occupied; **bytes
  1–2 = a 16-bit depth**, *excluded from `matches()`*; bytes 3–31 = the state
  key. So a 16-bit depth field **already exists** and is written each insert via
  `state.set_payload_depth(min(res.depth, UINT16_MAX))` (`solver.cpp:145`).
  Today it only biases replacement (shallower states kept in slot 0).
- **`insert_t` does *not* update an existing entry** — a re-arrival at a cached
  key returns `false` and changes nothing (the first-seen depth stays).
- The flat cache **evicts** (depth-preferred) and has **no explicit ancestor
  protection** — there is no "live" bit. (More in §3.7.)
- `PredecessorPolicy`'s depth field is only **8 bits** (`predecessor_state`).

**LRU cache** (`lru_cache`, `global_cache.{h,cpp}`) — Boost MultiIndex,
pile-order canonicalised, true LRU eviction:

- Each `cached_game_state` has a **mutable `live` bit** = "is an ancestor of the
  current node". Set on insert, cleared on backtrack (`set_non_live`). Eviction
  skips live entries; if *all* entries are live → `runtime_error` →
  `MEM_LIMIT`. This is the "never evict ancestors" rule.
- `lru_cache` already supports in-place mutation via `cache.modify(...)`. Adding
  per-entry status/budget fields here is straightforward.

### 2.3 Which invariants break under a depth bound

| Current invariant | Why it holds now | Why it breaks under bound `L` |
|---|---|---|
| "In cache → prune" is always sound | unbounded ⇒ a cached node's subtree is fully explored | a node may have been searched only to a small remaining budget (truncated); re-reaching it with more budget could reveal a win |
| First arrival depth is irrelevant | unbounded ⇒ subtree fully explored regardless of arrival depth | reaching a node via a **shorter path** gives **more budget** ⇒ must re-expand (Akagi's "revisit via shorter path"; review's "min arrival depth / re-open") |
| Exhaustion ⇒ unwinnable | nothing was ever cut | a branch cut at `L` is an **unexplored leaf**; exhaustion-to-`L` only means "no win *within* `L`" |
| Cache entries never need a status | every cached node is ancestor-or-fully-explored | we must now distinguish **`DEAD`** (fully explored, untruncated) from **`OPEN`** (truncated below) — "we have to mark nodes as definitely finished or not" |

The rest of this proposal is about restoring soundness for each row.

---

## 3. Theory: the soundness framework

This section formalises the author's scheme and shows it is a satisficing
specialisation of a **proven** algorithm. Notation:

- **`L`** — the depth bound for the current pass (max moves from root). A node at
  depth `d` is *expandable* iff `d < L`; at `d == L` it is a **truncated leaf**.
- **`d(n)`** — depth of node `n` = number of moves from root (counts dominance
  and K+ moves; same counter as `res.depth`).
- **`B(n) = L − d(n)`** — **remaining budget** at `n`.

### 3.1 Two verdicts, with opposite monotonicity

- **WIN is monotone in budget.** If a goal is reachable from `s` within `B`
  moves, it is reachable within any `B' ≥ B`. A `WIN` verdict is therefore valid
  on *any* later revisit, regardless of depth. In **satisficing** search the
  first `WIN` ends the whole search, so we never store/reuse `WIN` — we just
  return `SOLVED`. (This is exactly why JAIR §5.2 can cite Akagi et al. 2010 and
  say sub-optimality "is not an issue for us".)

- **Unwinnable is established only by exhaustion**, and the bound must respect it.
  `DEAD(s)` means *"the subtree at `s` was fully explored with no goal and **no
  truncated leaf anywhere below it**"*. That is a budget-independent certificate
  ("no goal below here, ever"), analogous to an inductive unsolvability
  certificate (Eriksson, Röger & Helmert 2017/18) or a learned clause in CDCL:
  sound to keep across re-bounds precisely because it was established *without
  reference to the bound*.

### 3.2 The three cache statuses

| Status | Meaning | Trust rule |
|---|---|---|
| `DEAD` | subtree fully exhausted, **no** truncated leaf below, no goal | **always** prune; budget-independent; safe to keep across all passes |
| `OPEN(b)` | subtree searched, no goal found, but ≥1 truncated/again-open leaf below; `b` = the **maximum remaining budget** under which it has been searched | prune **iff** `b ≥ B_now`; else **re-open** |
| *(absent)* | never visited (or evicted) | expand normally |

Plus a transient marker, **`ON_PATH`** = "ancestor in the current frontier"
(subtree mid-exploration). On revisit it is a cycle → prune (§3.5).

`WIN` is intentionally absent (folded into `SOLVED`).

### 3.3 The reuse inequality (the crux)

On reaching `s` at depth `d`, budget `B_now = L − d`:

- `DEAD` → **prune.**
- `OPEN(b)`:
  - `b ≥ B_now` → **prune.** Everything reachable now within `B_now` was already
    covered by the earlier, deeper-budget search. *(Sound for WIN: any goal
    within `B_now ≤ b` would have been found.)*
  - `b < B_now` → **re-open / expand.** We now have strictly more budget than `s`
    was ever searched with; a win may lie just beyond the old horizon.

This is the standard depth-tagged transposition-table discipline: Reinefeld &
Marsland (1994) "prune iff remaining bound ≤ stored bound"; the chess rule "use
the stored result iff stored depth ≥ needed depth". The author's own observation
— *"a node searched in this run but at a deeper depth than the current node"
should be re-searched* — is exactly `b < B_now` once you note that a *deeper
recorded depth* means a *smaller recorded budget*.

**Generations are not required for soundness if `b` is stored as an absolute
budget.** Because `b` is an absolute number of moves (not a depth relative to one
pass's `L`), the inequality `b ≥ B_now` is correct no matter which pass wrote the
entry. An entry written in an earlier, smaller-`L` pass simply has a small `b`,
so on the next (larger-`L`) pass `b < B_now` triggers re-open automatically. The
"generation marker / cleanse" the author describes is an **alternative encoding**
of the same idea (store `depth + generation` and compare within a generation);
the two are equivalent (see §3.8). We recommend absolute-budget storage as the
primary scheme and discuss the generation encoding as a memory-saving variant.

### 3.4 Min-arrival-depth and re-opening (DAG re-expansion)

The state graph is a heavily-transposing DAG, so `s` has no unique depth. The
only sound budget tag is computed from the **minimum arrival depth** `g_min(s)`
(equivalently the **maximum** remaining budget). Two consequences:

1. When `s` is later reached at a **new minimum** depth (`d < g_min(s)`), any
   cached `OPEN` failure is stale and `s` must be **re-opened** — exactly A\*/
   Dijkstra node re-opening on finding a cheaper path, and Akagi's DFSTT3
   "revisiting a node already reached via a shorter path".
2. `g_min` and `b` are **monotone** (depth down, budget up) and must be updated
   on every cheaper arrival. Treating them as write-once would make the
   `OPEN`-reuse rule unsound.

`WIN`/`DEAD` are untouched by re-arrival — they are budget-independent.

### 3.5 Cycles and Graph-History Interaction — the hard part

This is the single most important correctness subtlety, and the place where the
naive versions of the author's scheme go wrong. Solvitaire graphs **do** contain
cycles: JAIR §5.2 handles "a sequence of moves which arrives in a state
previously visited as a parent of the current node", and pins ancestors
specifically because "otherwise loops can occur". `creates_immediate_loop()` and
the K+ representation suppress *some* loops, not all.

Consider a back-edge `a → … → s → a` where `a` is an `ON_PATH` ancestor of `s`.
When we finalise `s`, `a` is **not yet finalised** (we are still inside its
subtree). How should the cycle edge `s → a` contribute to `s`'s status?

- **Naive choice 1 — "cycle taints `s` OPEN".** *Sound but incomplete.* The taint
  propagates up the whole cycle, so `a` itself never becomes `DEAD` — even at
  `L = ∞`. Any reachable cycle would then prevent us from *ever* proving
  unwinnable. ✗ (breaks goal 2).

- **Naive choice 2 — "cycle is a fully-closed edge (contributes ∞)".**
  *Complete but UNSOUND.* If, **after** `s` is finalised `DEAD`, exploration of
  `a`'s *other* children hits a truncation, then `a` has an unknown region — and
  `s` can reach it *through the cycle*. Marking `s` `DEAD` was wrong. ✗ (breaks
  goal 2 the other way).

The current unbounded code escapes this dilemma only because **nothing is ever
truncated**: with `L = ∞`, `a` is either fully explored (no goal ⇒ global
exhaustion ⇒ unwinnable) or a goal ends the search. Truncation is what
introduces GHI.

**The correct resolution is the IDA\*+TT algorithm DFSTT3** (Akagi, Kishimoto &
Fukunaga 2010, Fig. 8) — the same paper JAIR already cites. DFSTT3 stores per
node both an estimate `esti` (a lower bound on cost-to-goal; `esti = ∞` ≙ our
`DEAD`) **and** the `g`-cost at which it was searched, and backs `esti` up as the
min over children with two rules:

- a child reached via a cycle, **or** via a path no shorter than its stored `g`,
  is "suboptimal" and contributes its **stored `esti`** (a finite lower bound) —
  *not* `∞` and *not* a fully-closed edge;
- a child reached via a strictly shorter path is **re-expanded**.

Theorem 2 of that paper proves DFSTT3 is **admissible and complete**: with bound
`MAX` = longest acyclic path, `esti(root) = ∞` **iff** no solution exists,
*regardless of cycles and regardless of TT replacement policy*. Specialising to
Solvitaire (unit edge cost, heuristic `h ≡ 0`, satisficing) gives exactly our
scheme:

- `esti(n) = ∞` ⟺ `DEAD(n)`.
- a finite `esti(n)` plays the role of our `OPEN` "verified budget".
- the `g`-cost test is our `g_min` re-open.
- iterating `L` upward to `MAX` is the outer loop; once `L ≥ MAX`, `esti(root)`
  decides winnability with certainty.

**Recommendation:** implement the cross-pass `DEAD`/`OPEN` bookkeeping (Stage 2,
§5) by *mirroring DFSTT3's `esti`/`g` backup rules for the cycle and
shorter-path cases*. We inherit its proof rather than re-deriving correctness per
case. The cycle case is the one place an implementer must not "optimise" — the
contribution of a back-edge must be the ancestor's current finite estimate, never
`∞`.

> **Why Stage 1 (§5) sidesteps all of this:** if a pass uses a *fresh* cache and
> we only ever claim `unwinnable` when **zero** truncations occurred in the whole
> pass, then GHI cannot bite. With no truncation, the bounded pass visits exactly
> the states an unbounded pass would and prunes only on genuine full-subtree
> hits or ancestors — identical to today's trusted behaviour. Cycles are mere
> "already visited, skip", exactly as now. GHI is introduced *only* by the
> cross-pass reuse of partially-explored (`OPEN`) nodes, which is Stage 2.

### 3.6 The "half-depth" re-expansion filter breaks completeness

The author floated re-expanding an `OPEN` node only when re-reached at `≤ ½` its
recorded depth (a cheaper, lossier version of `b < B_now`). This is a lossy
filter: a node resolvable only when reached at a depth strictly between `½·d_rec`
and `d_rec`, but never at `≤ ½·d_rec`, can stay unknown **forever**, even with
unbounded total budget. So this variant is **incomplete** (not merely slow), and
the stranded nodes are exactly the deep/hard ones the scheme is meant to help.

This is tolerable *only* under Solvitaire's statistics regime — the paper already
reports a bounded fraction of "unknown" instances and Wilson 95% confidence
intervals (target ±0.1%). It is acceptable **only if** the extra unknowns are
measured and stay inside that budget. **Default: do not use the half-depth
filter.** Use the sound `b < B_now` rule and control re-search churn instead via
(a) a large initial `L`, (b) how aggressively `L` grows, and (c) generation
aging (§3.8). Treat half-depth as a Stage-3 experiment with explicit
unknown-count reporting (§5).

### 3.7 Interaction with eviction and ancestor handling

- **Eviction only ever causes *re-search*, never an unsound prune** — *provided*
  `b` is absolute. Losing a `DEAD` entry means we may re-derive it (slower).
  Losing an `OPEN(b)` entry means we re-expand (sound). There is no eviction that
  turns a correct prune into an incorrect one, because we never trust an evicted
  entry. ✓
- **Pin terminal entries — this is what enables the depth collapse (§1.3).**
  `DEAD` and large-budget `OPEN` entries are the expensive-to-recompute,
  high-value information; under plain LRU / depth-TwoBig1 they can be thrown away,
  undermining the scheme. Crucially, the cross-pass depth collapse *depends* on
  `DEAD` verdicts surviving from one pass to the next: if a shallow `DEAD` is
  evicted before the next, deeper pass snakes back down to it, that pass will
  re-expand it and the snake will not be cut. Prefer a value-aware replacement
  that evicts shallow/low-budget `OPEN` first and keeps `DEAD`. (The flat cache's
  existing depth-preferred replacement already biases toward keeping shallow
  entries; we want it to also respect a `DEAD` bit.)
- **Ancestor / cycle safety must not depend on the cache.** The flat cache can
  evict an `ON_PATH` ancestor (no live bit). Under a bound this is *less*
  dangerous than today — a loop that re-expands an evicted ancestor is itself
  capped at depth `L` — but it would register as spurious truncation and block
  the unwinnable claim. The clean fix is an explicit **on-path set**: a small
  hash set of the Zobrist keys of the current frontier, `insert` on descend /
  `erase` on backtrack, giving `O(1)` ancestor detection that is independent of
  cache eviction and uniform across flat and LRU policies. (The LRU `live` bit
  already does this for LRU games; the on-path set generalises it and is what
  Stage 2 should use for the `ON_PATH` test.)

### 3.8 Generations as an alternative encoding (and the "cleanse")

Storing `b` absolutely needs enough bits (§6.3). A common memory-saving
alternative — and the one the author described — is to store, per `OPEN` entry, a
small **generation id** plus the **depth** it was searched at, and use the rule:

> prune iff *same generation* **and** *recorded depth ≤ current depth*; otherwise
> re-open.

Across generations every `OPEN` entry is automatically re-opened (different
generation), which is correct because each new generation has a larger `L`. This
is precisely the chess-engine "aging / ancient flag". A **cleanse** between
passes (or a global generation bump that makes all prior-generation `OPEN`
entries re-openable) then means: keep `DEAD` forever, treat last generation's
`OPEN` as stale. The two encodings are equivalent; choose on memory/clarity
grounds (§6.3). One genuine use for the generation stamp regardless of encoding:
**stale `ON_PATH` markers** left behind if a pass ends mid-search (timeout/mem);
stamping `ON_PATH` with the generation lets the next pass ignore stale ones
instead of mistaking them for live ancestors.

---

## 4. The algorithm

### 4.1 Single bounded pass (reference, recursive form)

Presented recursively for clarity; §6 maps it onto the existing iterative engine.
Returns one of `WIN` / `DEAD` / `OPEN(b)`; `b` is a remaining-budget verified
lower bound (the satisficing `esti`). `on_path` is the ancestor set (§3.7).

```
search(s, d):                      # d = depth of s; B = L - d
    if s.is_solved():   raise FOUND_WIN              # ends everything

    hit = cache.lookup(s)
    if hit == DEAD:           return DEAD            # budget-independent prune
    if hit == OPEN(b):
        if d >= g_min(s):                            # not a shorter path
            if b >= (L - d):  return OPEN(b)         # covered: prune
            # else b < budget: fall through and re-open
        # else d < g_min(s): shorter path: fall through and re-open

    if s in on_path:         return OPEN(esti(s))    # cycle: DFSTT3-suboptimal
                                                     # (finite contribution!)
    if d == L:                                       # truncated leaf
        any_truncation = true
        cache.put(s, OPEN(0), g_min=d)
        return OPEN(0)

    on_path.add(s); g_min(s) = min(g_min(s), d)
    moves = s.legal_moves()                          # + dominance forcing
    if moves empty:                                  # genuine dead end
        on_path.remove(s); cache.put(s, DEAD); return DEAD

    verified = +inf                                  # min over children of 1+child
    for m in moves:
        r = search(child(s,m), d+1)
        if r == DEAD:        child_b = +inf
        elif r == OPEN(bc):  child_b = bc
        verified = min(verified, 1 + child_b)
    on_path.remove(s)

    if verified == +inf:  cache.put(s, DEAD);          return DEAD
    else:                 cache.put(s, OPEN(verified)); return OPEN(verified)
```

Pass result, given `root`:

- `FOUND_WIN` raised → **`SOLVED`** (winnable).
- `search(root,0) == DEAD` → **`UNSOLVABLE`** (proven). Equivalent to
  *exhausted with `any_truncation == false`*.
- `search(root,0) == OPEN(_)` → **`BOUNDED_EXHAUSTED`** — no win within `L`,
  truncation occurred ⇒ deepen.

**Stage-1 simplification.** If we never reuse `OPEN` across passes (fresh cache
each pass), the per-node `verified`/`g_min` machinery collapses to a single
global boolean `any_truncation`, and the cache reverts to today's plain
"present?" set. The pass returns `SOLVED`, or `UNSOLVABLE` iff exhausted with
`any_truncation == false`, else `BOUNDED_EXHAUSTED`. This is provably sound and
complete (see §3.5 box) and is the recommended first implementation.

### 4.2 Outer iterative-deepening loop

```
solve(instance):
    L = L0(game)                 # generous initial bound, per game (§6.5)
    gen = 0
    cache = fresh()
    loop:
        any_truncation = false
        r = bounded_pass(L, cache, gen)         # §4.1
        if r == SOLVED:            return winnable
        if r == UNSOLVABLE:        return unwinnable
        # r == BOUNDED_EXHAUSTED (or MEM_LIMIT inside the pass)
        if L >= L_max or out_of_time or out_of_memory:
            return unknown                       # report as timeout-like
        L = grow(L)              # e.g. L *= 2   (§6.5)
        gen += 1
        # Stage 2: keep cache (DEAD persists; OPEN re-opened by budget/gen rule)
        # Stage 1: cache = fresh()
```

`grow` doubling gives a geometric schedule; total work is dominated by the last
pass when growth is geometric and the tree is roughly exponential, so the ID
overhead factor is bounded (cf. Korf 1985). With a large `L0`, the easy mass
finishes in pass 1 and never deepens — this is the configuration the JAIR
"overhead didn't pay off" experiment most likely lacked.

### 4.3 New solver result types

`solver_result::type` currently has `TIMEOUT, SOLVED, UNSOLVABLE, MEM_LIMIT,
TERMINATED`. Add:

- **`BOUNDED_EXHAUSTED`** — internal per-pass outcome: no win within `L`,
  truncation occurred. Drives the outer loop; never surfaces to the user as a
  final verdict.
- The final user-facing mapping is unchanged (`winnable` / `unwinnable` /
  `timeout`/`unknown`), so JSON/CSV consumers and the regression oracle schema
  (`outcome`) need no change for Stage 1. (Stage 2 may add diagnostic fields:
  final `L`, number of passes, per-pass node counts.)

---

## 5. Staged implementation plan

Each stage is independently shippable and independently testable. **Do not start
a later stage until the earlier stage passes differential testing.**

### Stage 0 — Measure before building (≈1–2 days, no algorithm change)

Add instrumentation (or mine existing `max_depth`) to record, per resolved
instance: depth at which the win was found / max depth of the unwinnability
proof; peak RAM; peak trail length; peak ancestor-pin count. Produce the **depth
distribution of solutions vs. deep outliers** across the regression seed sets and
a few hard games (Beleaguered Castle, Klondike, FreeCell, Spanish Patience).

**Directly probe the depth-collapse hypothesis (§1.3).** The key unknown is how
much shallower the *shortest-path* structure is than the DFS-snake length. Two
cheap proxies, both implementable before any algorithm change:

- For a deep outlier, record the **minimum depth at which each cached state was
  ever reached** (instrument the existing cache to track first-seen vs.
  min-seen depth). A large gap between a state's snake-depth and its min-reachable
  depth is direct evidence the collapse will work.
- Run the *unbounded* solver but with an artificially imposed depth cap and see
  at what cap the deep outliers still get *truncated* — i.e. how the
  truncation-frontier depth relates to the eventual full depth.

**Decision gate.** If most instances resolve below a modest `L` with a thin deep
tail, **or** the deep outliers show a large snake-vs-shortest-path gap → proceed
(the collapse should pay off). If solution depth is broadly distributed *and*
states are genuinely only reachable via long paths → the collapse will not fire,
depth bounding will manufacture unknowns → reconsider (and lean on
constraint-based unwinnability instead).

### Stage 1 — Sound bounded ID, fresh cache per pass (the safe core)

Smallest change that delivers shallow-win-finding and the per-pass RAM cap, with
**zero GHI risk and zero cache-format change**. Note Stage 1 does **not** deliver
the cross-pass *depth collapse* (§1.3) — with a fresh cache each pass can re-snake
to full depth — so for the deep-*unwinnable* tail Stage 1 may still need `L` near
the unbounded depth. Stage 1 is the correctness foundation; Stage 2 is where the
collapse (and the main payoff) appears.

- Add `--depth-bound`, `--depth-grow`, `--max-depth-bound` CLI options.
- In `dfs()`: refuse to expand at `res.depth >= L` (both the dominance branch and
  the legal-moves branch); set `any_truncation = true` there.
- Add `BOUNDED_EXHAUSTED`; map "exhausted ∧ ¬any_truncation" → `UNSOLVABLE`,
  "exhausted ∧ any_truncation" → `BOUNDED_EXHAUSTED`.
- Outer loop in `solve_game_impl` (`main.cpp`): re-construct cache + solver per
  pass, growing `L`, until `SOLVED`/`UNSOLVABLE`/limits.

**Validation gate.** Differential test against current Solvitaire on the existing
oracles (levels 1–2 at least): with `L = ∞` the behaviour must be byte-identical
(trace gate); with finite `L`, *every* `SOLVED`/`UNSOLVABLE` verdict must match
the unbounded verdict (never a disagreement), and we expect equal-or-shallower
solution depths and reduced peak RAM on the deep outliers.

### Stage 2 — Cross-pass cache reuse (`DEAD` retention + `OPEN(b)` + `g_min`)

The "reuse the cache / cleansed version" the author wants. This is where the real
subtlety **and the main payoff** live: persistent `DEAD` retention is the
mechanism that produces the cross-pass **depth collapse** (§1.3) — the deep snake
of a later pass is cut at shallow `DEAD` nodes proven in earlier passes — which is
the difference between this scheme and the vanilla ID the paper found didn't pay
off.

- Add per-entry `status` (`DEAD`/`OPEN`), `b` (verified budget), `g_min` (min
  arrival depth) — see §6.3 for layout per cache.
- `insert` must support **update-on-hit** (raise `b`, lower `g_min`, upgrade
  `OPEN→DEAD`); today's flat `insert_t` no-ops on hit.
- Implement the reuse rule (§3.3/§3.4) and the **DFSTT3 cycle/shorter-path
  backup** (§3.5) via the explicit **on-path set** (§3.7).
- Keep the cache across passes (or use the generation/cleanse encoding §3.8).
- Replacement: pin `DEAD`; evict shallow `OPEN` first.

**Validation gate.** Differential test vs. Stage 1 *and* vs. current Solvitaire:
identical `winnable`/`unwinnable` verdicts on all oracle instances; measurable
reduction in total nodes searched across the ID sequence vs. Stage 1 (reuse
working); no increase in unknown count. Add targeted **cycle/GHI unit tests**
(small hand-built games with known back-edges and a truncation behind the cycle)
that would fail under either naive choice in §3.5.

### Stage 3 — Optional churn control (measure incompleteness)

Only if Stage 2 re-search churn is too high. Options, each behind a flag and each
reported with resulting unknown-count and CI widening: generation aging tuning;
the lossy half-depth filter (§3.6, expect incompleteness); Luby-style restarts
with the retained terminal cache (the SAT "restart but keep learned clauses"
analogue) to attack the "early wrong turn" pathology.

---

## 6. Concrete code touch-points

### 6.1 `solver.cpp` / `solver.h`

- `solver_result::type`: add `BOUNDED_EXHAUSTED`; update `operator<<`.
- `solver_impl`: carry `L`, `generation`, `bool any_truncation`, and (Stage 2)
  the `on_path` hash set. `run()`/`dfs()` take the bound.
- `dfs()`: the depth-limit cut goes **before** both the dominance push
  (`solver.cpp:133`) and the legal-move expansion (`solver.cpp:175`); set
  `any_truncation` and back out. Stage 2: replace the "already present → backtrack"
  branch (`solver.cpp:190`) with the §3.3 reuse decision (prune vs. re-open), and
  finalise `DEAD`/`OPEN(b)` in `revert_to_last_node_with_children` when a node's
  children are exhausted.
- The explicit `frontier` already gives us the full path for `on_path`
  maintenance; we only need to also keep a hash set for `O(1)` membership.

### 6.2 Outer loop (`main.cpp`)

- The natural home is `solve_game_impl<Policy>` (it already constructs
  `game_state`, cache, solver, and runs once). Wrap the run in the §4.2 loop. The
  existing `smart` retry in `solve_game()` is a precedent for "run, inspect,
  re-run" but builds a *fresh* cache; Stage 2 instead keeps the cache and calls
  the solver repeatedly with a growing `L` (re-seat the solver on the same cache,
  or add `solver::run(L)` that resets the frontier but not the cache).

### 6.3 Per-entry metadata layout (the bit-width question)

Stage 1 needs **none** of this (global flag only). Stage 2 needs, per cache key,
roughly: `g_min` (min arrival depth) and `b`/`status` (verified budget, with a
`DEAD` sentinel). Depths reach `1.9 × 10⁸`, so **16 bits is not enough** and even
32 bits only just covers the observed maximum — the existing `compact_state`
depth field (16 bit) and `predecessor_state` depth field (8 bit) must be treated
as replacement-hints only, not as `g_min`/`b`.

Options (recommend deciding in review):

- **(A) Parallel metadata array** for the flat cache: a separate
  `meta[num_clusters]` of `{uint32 g_min; uint32 b}` per slot (16 B/cluster),
  allocated next to the 64 B key clusters via the same `platform::lazy_buffer`
  mechanism. Keeps the key cluster cache-line-clean; metadata fetched only on a
  hit. **Recommended.**
- **(B) Widen the entry.** `compact_state` 32→40 B breaks the 64 B cluster
  static-asserts and the cache-line story; intrusive.
- **(C) `DEAD` bit in the key entry + side map for `OPEN` only.** Most explored
  subtrees become `DEAD` once `L` is large; store a single `DEAD` bit cheaply in
  the key entry (steal a byte) and keep `(g_min, b)` in a small open-addressed
  side table for the `OPEN` minority near the truncation frontier. Most
  memory-efficient; slightly more code.
- **(D) Generation encoding** (§3.8): store `gen` (≈8 bit) + `depth` (needs ~28
  bit). Saves nothing over absolute `b` here because `depth` is the wide field;
  its only advantage is the trivial cross-pass cleanse.

The **LRU cache is easy**: add `status`, `b`, `g_min` to `cached_game_state` and
mutate via the existing `cache.modify(...)` (as `set_non_live` already does).

### 6.4 What "insert" must become (Stage 2)

`generic_flat_cache::insert_t` currently returns `false` and does nothing on a
hit. Stage 2 needs an `upsert` that, on a hit, may *raise* `b`, *lower* `g_min`,
and *upgrade* `OPEN→DEAD`, returning enough information for the solver to decide
prune-vs-reopen. Cleanest as a new method (`probe_and_update`) returning a small
struct `{present, status, b, g_min}`, leaving `insert_t` for Stage-1/legacy use.

### 6.5 Initial bound `L0`, growth, and depth units

- **K+ and dominance count toward depth.** A depth bound applies to a *non-uniform*
  notion of "one move" (K+ compresses several stock moves into one; dominance
  moves are forced singletons). Define `L` on the same move counter used by
  `res.depth` — consistent, if coarse.
- **`L0` should be per game** (or adaptive from Stage 0 data). A single fixed
  `L0` (say `10⁶`) is far more binding in some games than others because K+
  changes the branching/depth trade-off per game. Suggest `L0 ≈` a high
  percentile of the Stage-0 solution-depth distribution for that game, with
  geometric growth (`×2`) thereafter.

---

## 7. Correctness and testing strategy

1. **`L = ∞` identity.** With the bound disabled the solver must reproduce
   today's search exactly — verifiable on the existing **trace gate**
   (`cmake-build-trace`, `trace_identity_*`, `trace_regression_level1/2`).
2. **Differential verdicts.** For every oracle instance, the bounded/ID solver's
   final `winnable`/`unwinnable` verdict must equal the unbounded verdict. Any
   single disagreement is a hard failure. Run across levels 1–2 routinely, 3–5
   before merge.
3. **Soundness invariants (assertions, debug builds).** (a) a node finalised
   `DEAD` had no truncated leaf and no unresolved back-edge below it; (b) a prune
   at `OPEN(b)` only happened with `b ≥ B_now`; (c) `any_truncation == false` ⟺
   root finalised `DEAD`; (d) no `ON_PATH` marker survives a completed pass.
4. **GHI regression tests.** Hand-built minimal games with a deliberate cycle and
   a truncation hidden *behind* the cycle (the §3.5 unsound case), asserting the
   solver does **not** report `unwinnable` until `L` is large enough to expose the
   truncation. These tests should fail under either naive cycle rule.
5. **Memory.** On the deep outliers, peak RSS (the JSON `solver_resident_bytes`
   path already exists in `main.cpp`) must drop materially at finite `L`.
6. **Unknown-count budget.** Track the fraction of instances ending `unknown` at
   the chosen `L_max`/time budget; for any lossy variant (Stage 3) report the
   Wilson CI widening explicitly and keep within the paper's ±0.1% (±0.2% where
   already accepted).

---

## 8. Risks and limitations

| Risk | Severity | Mitigation |
|---|---|---|
| Cycle/GHI handled wrongly → false `unwinnable` (unsound) or never-`unwinnable` (incomplete) | **High** | Mirror DFSTT3 exactly (§3.5); GHI unit tests (§7.4); Stage 1 avoids it entirely |
| `OPEN`/`g_min` bookkeeping cost over a huge DAG | Medium | parallel/side-table layout (§6.3); Stage 1 baseline has none |
| Eviction of `DEAD` entries → lost proofs, re-search | Low (sound, slower) | pin `DEAD` in replacement (§3.7) |
| Flat cache evicts an `ON_PATH` ancestor | Low under a bound | explicit on-path set (§3.7); bound caps any resulting loop |
| 16-/8-bit depth fields too narrow for `g_min`/`b` | Medium | widen via side array (§6.3); not needed in Stage 1 |
| Half-depth filter strands hard instances (incomplete) | Medium | off by default (§3.6); Stage 3 only, measured |
| Depth collapse (§1.3) fails to materialise — a game genuinely needs deep lines | Medium | bounded downside (≈2× overshoot + reuse-limited re-search); measure in Stage 0/2; constraint-based proofs (Dang et al. 2025) as a complementary route |
| ID re-search overhead ("didn't pay off") | Medium | large per-game `L0` so easy mass finishes in pass 1; Stage 2 reuse; geometric growth |

---

## 9. Open questions for the author

See [`open-questions.md`](open-questions.md) for the consolidated list with
context. The highest-leverage ones:

1. **Primary objective?** Shallow-win-finding on the winnable deep tail, vs. peak
   RAM reduction, vs. an anytime/"probably unknown" mode — these weight the
   stages differently.
2. **Is any incompleteness acceptable** (the half-depth filter, traded for less
   churn within the ±0.1% unknown budget), or must the scheme stay complete in
   the limit?
3. **Cross-pass reuse encoding:** absolute budget `b` (§3.3, recommended) vs. the
   generation/cleanse encoding you described (§3.8)?
4. **Per-entry metadata layout** for Stage 2 (§6.3 options A–D)?
5. **`L0` and growth policy** — per game from Stage-0 data? doubling?
6. Confirm cycles genuinely occur in the **flat-cache** games (not just LRU), so
   we size the GHI work correctly. (We can answer this empirically in Stage 0.)

---

## 10. References

- Blake, C. & Gent, I. P. (2026). *The Winnability of Klondike Solitaire and Many
  Other Patience Games.* JAIR 85, Art. 21. doi:10.1613/jair.1.17167. (§5.1 DFS,
  §5.2 transposition tables; cites Akagi et al. 2010.)
- Akagi, Y., Kishimoto, A. & Fukunaga, A. (2010). *On Transposition Tables for
  Single-Agent Search and Planning: Summary of Results.* SoCS-10.
  doi:10.1609/socs.v1i1.18164. (**DFSTT1/2/3**; DFSTT3 = admissible **and**
  complete; the formal backbone of §3.5.)
- Reinefeld, A. & Marsland, T. A. (1994). *Enhanced Iterative-Deepening Search.*
  IEEE TPAMI 16(7). (Depth-tagged TT; "prune iff remaining bound ≤ stored bound".)
- Korf, R. E. (1985). *Depth-First Iterative-Deepening.* AIJ 27(1).
- Dang, Gent, Nightingale, Ulrich-Oltean & Waller (2025). *Constraint Models for
  Klondike.* CP 2025. (Complementary, sound unwinnability proofs.)
- Eriksson, Röger & Helmert (2017/2018). *Unsolvability certificates / a proof
  system for unsolvable planning tasks.* (Inductive-certificate view of `DEAD`.)
- Internal: `docs/depth-bounded-search/` (this folder); the prior review
  *"Depth-Bounded, Cache-Reusing Search for Solvitaire: Literature Review and
  Technical Assessment"* (the WIN/DEAD/OPEN\@B\* framing, the `B* ≥ B_now`
  inequality, min-depth/re-open, truncation-tainting, half-depth incompleteness).
