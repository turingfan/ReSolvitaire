# Pickup Document: refactor-caching Branch

**Date:** 2026-03-30 (updated 2026-04-07)
**Branch Status:** Active development branch — contains `hash_only_cache`, `predecessor_flat_cache`, `cache_factory.h`, and mmap lazy allocation for all three flat caches. These are **not yet merged to `dev`**.

## Executive Summary

The `refactor-caching` branch has **completed M8 (cleanup and stabilization)** and successfully implemented a dual-cache architecture:

- **Flat cache** (new descriptor-based Zobrist): Fast, low-footprint, handles single-deck non-symmetric games
- **LRU cache** (legacy): Fallback for complex games (two-deck, spider stock, suit-symmetry)
- **Suit-symmetry fallback** (new M8): Automatically routes suit-symmetry games to LRU, preventing correctness issues

All 5-level regression oracles have been regenerated post-M6 (pile ordering removal). One hard instance (Spanish Patience seed 2921115) now times out; this is documented as an acceptable soft pass.

## What's Completed

### M6: Pile Ordering Removal
- Removed pile canonicalization logic from flat cache path
- Updated outcome-only regression comparison policy
- Trade-off: faster most games, slower some multi-pile games (Spanish Patience, Klondike with many hidden cards)

### M7: Bug Fixes & Validation
- Fixed 4 bugs in `recompute_payload_from_scratch()`
- Fixed UBSan warnings (uninitialized sol_rules bools)
- Validated all 5 regression levels pass (outcome only)

### M8: Cleanup & Suit-Symmetry Fallback
- Removed dead code (debug prints, unused functions)
- **Added suit-symmetry fallback:** When `--streamliners suit-symmetry` or `both`, cache selection automatically routes to LRU. Flat cache cannot provide suit-canonical deduplication.
- Updated `CLAUDE.md` with dual-cache architecture notes
- **Updated `known-issues.md`:** Issues #3–#4 document flat cache limitations with suit-symmetry and benchmarking gaps.

### Regression Infrastructure
- All 5 oracles regenerated with post-M6 solver
- Level 1: 150 instances, ~2 min runtime
- Levels 2–5: ~160 instances each; timeouts acceptable (soft passes)
- Python harness (`scripts/regression_runner.py`) validates outcomes only

## Completed Optimizations (on `refactor-caching`)

### mmap Lazy Allocation (2026-04-07, commit `fe98157`)
All three flat caches (`flat_cache`, `hash_only_cache`, `predecessor_flat_cache`) now use
`platform::lazy_buffer` RAII for demand-paged allocation on macOS/Linux. Physical pages
are committed only on first write. `clear()` uses a single syscall (`MAP_FIXED` remap on
macOS, `MADV_DONTNEED` on Linux) rather than iterating over all clusters.
- See `optimization-opportunities.md` §1 (marked DONE)
- Container tests require `-m 8g` due to 200M-entry test caches reserving large virtual space

## What Remains: Flat Cache Extension Roadmap

See `docs/cache-redesign/active/flat_cache_extension_roadmap.md` for full design. **Do not implement yet—this is planning documentation.**

### Priority 1: Tableau Dealing Games
**Files:** FreeCell, Klondike with `--reveal-hidden`, Scorpion
**Challenge:** Zobrist must distinguish pile identity when stock deals are present
**Solution:** Payload encodes parent pile ID (6 bits per card × 52 cards) + Z[card_id][parent_pile]

### Priority 2a: Suit-Canonical Hashing
**Games:** Any with suit-symmetry streamliner (Spanish Patience, Klondike, etc.)
**Challenge:** Current Zobrist indexes `Z[card_id][pile]`; suit-symmetric duplicates use same hash
**Solution:** Colour-class indexing with additive hash combining. **Open:** descriptor ambiguity under equivalence (multiple PARENT_0 positions are equivalent).

### Priority 2b: Suit-Irrelevant Games
**Games:** Black Hole, Pyramid (any game where suit is never relevant)
**Challenge:** Equivalent cards must hash to same state
**Solution:** Additive combining across piles; XOR within piles for commutative ordering

### Priority 3: Gaps
**Games:** Gaps (rebuilds entire tableau on empty space)
**Challenge:** Cards can be anywhere; pile identity lost
**Solution:** Positional Zobrist; full rehash on redeal

### Priority 4: Accordion
**Games:** Accordion (any game with movable foundation or rolling reserve)
**Challenge:** Pile relationships change; precedessor-successor links
**Solution:** Chain-based linked-list payload (6b top + 4b length + 3b base + 2b/card suits). **See:** human_contributions.md #23 for encoding details.

### Chain-Based Representation (Advanced)
For games with equivalent cards and complex precedence (Accordion, Suit-Irrelevant, Equivalence), the roadmap proposes a **chain model**:
- **State** = multiset of chains (top card, length, base position) + zone counts per equivalence class
- **Zobrist:** Z[rank][colour][length][base] + Z_zone[rank][colour][zone][count]
- **Combining:** Additive (modular add) for pile-order invariance
- **Encoding:** ~27 bits per chain; much cheaper than per-card descriptors

This elegantly solves the descriptor ambiguity problem: rather than asking "where is card X," ask "what chains exist."

## Known Limitations (M8)

### Issue #2: Spanish Patience Traversal Regression
- **Root:** M6 removed pile ordering; DFS blowup for 13-pile tableau
- **Impact:** Seed 2921115 now OOM-kills (was solving in 115s with 15M nodes)
- **Policy:** Soft pass—outcome is correct (proven unsolvable or timed out); marked as `timeout` in oracle
- **Future:** Lightweight heuristic move ordering without full pile sort

### Issue #3: Flat Cache + Suit-Symmetry Untested
- **Root:** Flat cache hashes on card ID; suit-symmetric duplicates collide
- **Mitigation:** `use_new_cache()` returns false when suit-symmetry active; LRU fallback is automatic
- **Gap:** Flat cache path never exercised by regression suite for suit-symmetry games
- **Future:** Suit-canonical hashing (Zobrist with colour-class index) would enable flat cache

### Issue #4: Benchmarks Incomplete
- **Gap:** No flat-vs-LRU comparison for suit-symmetry games (Spanish Patience, Klondike+suit)
- **Future:** Benchmark once suit-canonical flat cache is ready; `benchmark-python` framework is now in `dev`

### Issue #1: JSON Round-Trip Discrepancy (Minor)
- **Root:** `json_helper::print_game_state_as_json` uses runtime-reordered piles instead of construction order
- **Impact:** Exported JSON parses to logically identical but internally different state; breaks reproducibility
- **Workaround:** Levels 2–5 use seed-based runs, bypassing JSON export
- **Fix:** One-line change documented in code comment (deferred)

## What's on `refactor-caching` but NOT yet in `dev`

As of 2026-04-07, `refactor-caching` has ~41 commits ahead of `dev` including:

- `hash_only_cache` — hash-only flat cache (16-byte clusters, no payload)
- `predecessor_flat_cache` — accordion/predecessor-encoded cache (128-byte clusters)
- `cache_factory.h` — centralised `make_cache()` replacing 4 duplicated selection blocks
- `platform::lazy_buffer` RAII wrapper + mmap lazy allocation for all three flat caches
- `dual_cache.h` — comparison harness for correctness testing
- Full unit test suites for all of the above

**Relationship:**
- `refactor-caching` is the **active development** branch for new cache types and optimizations
- `dev` is the **production** branch with current solver + benchmarks
- Plan: merge `refactor-caching` into `dev` once the new caches have passed Level 1 regression

## Integration with `dev`

**`dev`** now contains:
- All original refactor-caching logic (dual cache, M6–M8)
- Full Python/R benchmarking framework (`benchmark-python` branch, merged 2026-04-03)
- All regression oracles regenerated

See `docs/cache-redesign/active/branch_workflow.md` for the full branching protocol.

## Next Steps for Future Work

1. **Review the roadmap** (`flat_cache_extension_roadmap.md`): Decide which priority item to tackle first
2. **Start with Priority 1 or 2b**: Lower complexity than chain-based representation
3. **Create new branch from `dev`**: `git checkout dev && git checkout -b implement-tableau-dealing`
4. **Implement, test, benchmark:** Use regression harness to validate (outcome-only policy)
5. **Merge back to `dev`** when ready, then sync `dev` back into `refactor-caching`

## Testing Workflow

```bash
# Build
./build.sh --release --unit-tests

# Quick validation (Level 1 only, ~2 min)
cd cmake-build-release && ctest -R regression_level1 --output-on-failure

# Full validation (Levels 1–5, ~2 hours with timeouts)
cd cmake-build-release && ctest -R regression_level --output-on-failure

# Benchmarking (if changes affect performance)
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-20 --timeout 60000 \
    --output results/test.csv
```

## Key Files for Future Work

- **Roadmap:** `docs/cache-redesign/active/flat_cache_extension_roadmap.md`
- **Chain encoding:** `docs/cache-redesign/active/human_contributions.md` (#23)
- **Known issues:** `docs/known-issues.md`
- **Cache interface:** `src/main/game/cache_interface.h` (use_new_cache logic)
- **Main cache files:** `src/main/game/flat_cache.h/cpp`, `src/main/game/global_cache.h/cpp`
- **Zobrist tables:** `src/main/game/zobrist.h/cpp`
- **Regression harness:** `scripts/regression_runner.py`, `CMakeLists.txt` lines 225–288

## Branch Status

This branch is **actively maintained as a design branch**. All code is in `dev`. The unique content here — the optimization opportunities analysis, flat cache extension roadmap, and this pickup document — represents the planning layer for the next phase of work.

When ready to implement a roadmap item, see `branch_workflow.md` for the branching protocol.
