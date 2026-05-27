# Stage 2B — Implement `in_space(k)` Pile-Indexed Locatives

You are implementing a missing feature in the multiplicity encoding. The v5 spec defines
`in_space(k)` — pile-indexed locative descriptors for games where tableau pile identity
matters (TABLEAU_PILES stock dealing, i.e. Spider-type games). The current implementation
uses bare `MLD_IN_SPACE` for all pile bottoms, which is correct for pile-symmetric games
(Klondike, FreeCell) but wrong for TABLEAU_PILES games where stock cards are dealt to
specific piles by index.

Read CLAUDE.md for build/test commands and `01-Knowledge-Base/AGENTS.md` for mandatory
process rules. All 3 test gates must pass before committing.

---

## Background

**v5 spec §3 (descriptor enum):**
```
DK_IN_SPACE = 6,    // and 6+k for tableau pile k
```

**v5 spec §3 (descriptor table):**
> `in_space(k)` — Bottom of pile k (pile-symmetric: k omitted, treated as k = 0)

**When piles are pile-symmetric (k omitted):**
All non-TABLEAU_PILES games. Empty spaces are interchangeable. All pile bottoms get
`MLD_IN_SPACE` (= 6). This is the current behaviour and is correct.

**When piles are NOT pile-symmetric (k used):**
Games with `rules.stock_deal_t == sol_rules::stock_deal_type::TABLEAU_PILES`. The stock
deals card i to tableau pile i, so pile identity matters. Pile bottoms must get
`MLD_IN_SPACE + pile_index` where pile_index is the pile's position in
`original_tableau_piles` (0-based).

**Single-deck TABLEAU_PILES games:** east-haven, spiderette, will-o-the-wisp (all 7 piles).
Locative kinds 6..12, Zobrist columns 58..64 — well within the table's 80 columns.

---

## Changes Required

### 1. `src/main/game/multiplicity_descriptor_engine.h` — `recompute_all()`

The tableau loop (currently around line 171) iterates over `ctx.original_tableau_piles`.
Change it to track pile index and use `MLD_IN_SPACE + pile_idx` when piles are not
symmetric:

```cpp
// Determine whether pile identity matters
bool pile_sym = (ctx.rules.stock_size == 0
    || ctx.rules.stock_deal_t != sol_rules::stock_deal_type::TABLEAU_PILES);

uint8_t pile_idx = 0;
for (pile::ref tab_ref : ctx.original_tableau_piles) {
    const pile& p = ctx.piles[tab_ref];
    uint8_t space_kind = pile_sym
        ? MLD_IN_SPACE
        : static_cast<uint8_t>(MLD_IN_SPACE + pile_idx);
    for (pile::size_type i = 0; i < p.size(); i++) {
        card c = p[i];
        uint8_t cid = card_cid(c);
        if (i + 1 == p.size()) {
            descriptors[cid] = multiplicity_descriptor::make_locative(space_kind);
        } else {
            card parent = p[i + 1];
            descriptors[cid] = multiplicity_descriptor::make_predecessor(
                card_cid(parent), c.is_face_down());
        }
    }
    pile_idx++;
}
```

### 2. `src/main/game/multiplicity_descriptor.h`

Update the comment on `MLD_IN_SPACE` and `MLD_COUNT`:

```cpp
MLD_IN_SPACE   = 6,   // Bottom of tableau pile; +k for pile k when not pile-symmetric
// Values 7..27: MLD_IN_SPACE+1 through MLD_IN_SPACE+21 for pile-indexed games,
// plus headroom for future locative kinds.
```

Update `MLD_COUNT` to reflect the maximum locative kind actually used. With up to 21
piles (the spec's single-deck limit), the max kind is 6+20=26, so:

```cpp
static constexpr uint8_t MLD_MAX_LOCATIVE = 27;  // max locative kind value (inclusive)
```

Or simply remove MLD_COUNT if it's unused. Check whether anything references it.

### 3. `src/main/game/cache_interface.h` — remove TABLEAU_PILES exclusion

Change `use_multiplicity_cache()` to:

```cpp
inline bool use_multiplicity_cache(const sol_rules& rules,
                                    bool /*suit_symmetry_active*/ = false) {
    return !rules.two_decks
        && rules.sequence_count == 0
        && rules.accordion_size == 0;
}
```

The `stock_deal_t != TABLEAU_PILES` guard is no longer needed. Note: `use_new_cache()`
retains its TABLEAU_PILES exclusion (FlatPolicy still can't handle pile-indexed games).

### 4. `src/main/game/search-state/game_state.cpp` — update asserts

Lines 895 and 915 have:
```cpp
assert(!use_new_cache(rules));  // KI-6: TABLEAU_PILES games always use LRU cache
```

These fire in debug builds when MultiplicityPolicy is used for TABLEAU_PILES games.
Change to:
```cpp
assert(!use_new_cache(rules) || use_multiplicity_cache(rules));
```

Or more precisely, since the assert is about confirming TABLEAU_PILES games don't use
the flat cache's incremental descriptor path:
```cpp
assert(!use_new_cache(rules));  // TABLEAU_PILES games use LRU or multiplicity cache
```

Wait — `use_new_cache()` still excludes TABLEAU_PILES, so the assert will still pass.
The comment is misleading though. Update the comment:
```cpp
assert(!use_new_cache(rules));  // TABLEAU_PILES: not eligible for flat cache
```

Actually, verify: does `use_new_cache(rules)` return false for TABLEAU_PILES games?
Yes — it checks `stock_deal_t != TABLEAU_PILES`. So the assert is fine. Just update the
comment to remove the "always use LRU cache" claim.

### 5. Auto-dispatch in `main.cpp`, `benchmark.cpp`, `solvability_calc.cpp`

The current uncommitted changes add `suit_sym && use_multiplicity_cache(rules)` branches
to auto-dispatch. These are premature — they change the default cache for suit-sym games,
breaking regression oracles.

**For this task:** revert these dispatch additions. Multiplicity remains opt-in via
`--cache-type multiplicity`. The auto-dispatch question is separate (testing phase).

However, `--cache-type multiplicity` must now work for TABLEAU_PILES games since
`use_multiplicity_cache()` allows them. Verify that the explicit `multiplicity` path in
the dispatch chains calls `use_multiplicity_cache(rules)` without the old exclusion.

---

## What NOT to Change

- `recompute_from_descriptors()` — the fixpoint/sort/additive-hash algorithm is unaffected;
  it processes whatever locative kinds appear in descriptors[]
- `multiplicity_zobrist.h/cpp` — table already has 28 locative columns (52..79)
- `multiplicity_descriptor_store.h` — slot bytes just have higher locative_kind values
- `multiplicity_static_class.h` — class structure is independent of pile indexing
- Pile ordering in `place_card`/`take_card` — already bypassed for MultiplicityPolicy
  via `skip_pile_ordering = true`

---

## Testing

### Gate 1: All 3 test gates
```bash
python3 scripts/run_tests.py
```

### Gate 2: No-symmetry regression (existing games)
Run Klondike seeds 1-10 with `--cache-type multiplicity` (no streamliner). Must produce
identical `states_searched` to `--cache-type auto` on 0-eviction seeds. This confirms
pile-symmetric games are unaffected.

### Gate 3: TABLEAU_PILES games work
```bash
for game in east-haven spiderette will-o-the-wisp; do
    echo "=== $game ==="
    ./cmake-build-release/bin/solvitaire --type "$game" --random 1 \
        --cache-type multiplicity --timeout 30000 --json 2>/dev/null \
        | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['result'], 'states:', d['states_searched'])"
    ./cmake-build-release/bin/solvitaire --type "$game" --random 1 \
        --cache-type auto --timeout 30000 --json 2>/dev/null \
        | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['result'], 'states:', d['states_searched'])"
done
```
Solvability must agree between multiplicity and auto (LRU) for all seeds tested.

### Gate 4: Debug build doesn't assert
```bash
./cmake-build-debug/bin/solvitaire --type spiderette --random 1 \
    --cache-type multiplicity --timeout 10000
```
Must not trigger any assert.

---

## Constraints

- Do NOT modify any file outside the multiplicity-encoding scope
- Do NOT change `use_new_cache()` — FlatPolicy still can't handle TABLEAU_PILES
- Do NOT change the Zobrist seed or table dimensions
- Preserve pile-symmetric behaviour for non-TABLEAU_PILES games (k omitted)
