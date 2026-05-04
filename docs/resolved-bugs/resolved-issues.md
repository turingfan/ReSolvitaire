# Resolved Issues

This file documents known issues that have been resolved. Issues are listed by their
original KI number where assigned.

For earlier resolved bugs (pre-KI numbering) see the individual bug files in this
directory: `bug_root_descriptor_false_positives.md`,
`bug_starting_descriptor_not_position_canonical.md`,
`bug_waste_ptr_and_canfield_wrapping.md`.

---

## KI-1. JSON Deal Round-Trip Changes Node Counts (`json_helper.cpp`)

**Affected file:** `src/main/input-output/input/json-parsing/json_helper.cpp`
**Status:** RESOLVED — fix present on `dev` (commit `10c233c` and earlier merges)
**Impact:** Was: different `states_searched` counts when running from exported JSON vs seed

`json_helper::print_game_state_as_json` was serialising tableau piles by iterating
`gs.tableau_piles` (the runtime-reordered list) rather than `gs.original_tableau_piles`
(the fixed construction order). The code on `dev` now correctly uses
`gs.original_tableau_piles` (lines 87, 90 of `json_helper.cpp`).

---

## KI-8. Redundant Hash/Payload Computation in Default Binary for LRU Games

**Status:** RESOLVED by templated dispatch (Phase A, branch `feature/templated-dispatch`, Commits 0-6)
**Impact:** Was: wasted hash and payload computation on every DFS move for LRU games
**Proposal doc:** `docs/proposals/PROPOSAL-templated-game-state-dispatch.md`

`game_state_impl<Policy>` uses `if constexpr (Policy::computes_hash)` to eliminate all
hash/payload computation at compile time for `LRUPolicy`. `solver_impl<Policy>` holds
`Policy::cache_type&` directly — zero virtual dispatch in the DFS hot path. Legacy
concrete caches (`flat_cache`, `hash_only_cache`, `predecessor_flat_cache`) removed;
all flat variants now use `generic_flat_cache<ClusterPolicy>`.

---

## KI-11. Build Script Does Not Build Variant Binaries for Regression Tests

**Affected file:** `build.sh`
**Status:** RESOLVED — commit 0653486 (branch `fix/variant-build-hash-only`, merged to `dev`)
**Plan:** `docs/fix-variant-build-hash-only/implementation_plan.md`
**Impact:** Running regression tests required manual build commands; `./build.sh` alone was insufficient

The CMakeLists.txt defines three variant executable targets (`solvitaire-flat`,
`solvitaire-hash-only`, `solvitaire-lru`) configured with compile-time cache selection
flags. The regression test harness (CMakeLists.txt lines 225–288) invokes these three
binaries.

**Fix:** `build.sh` now accepts a `--variants` flag that builds all three variant targets
after the main build. `scripts/container-build.sh` forwards `--variants` to the inner
build and runs the variant regression tests. Usage:
```bash
./build.sh --variants
./scripts/container-build.sh --variants
```

---

## KI-12. Hash-Only vs Flat Cache Total Memory Usage Discrepancy

**Status:** RESOLVED: human error in running experiments. Led to optimisations separately.
**Impact:** Was: memory efficiency claims for hash-only cache not confirmed under realistic benchmarks

**Observed:** Cache cluster allocations are correct in theory:
- Hash-only clusters: 16 bytes (two uint64_t hashes)
- Flat cache clusters: 64 bytes (two 32-byte compact_state entries)
- Expected ratio: 1:4 (hash-only should use 1/4 the cache memory)

Measured allocations match theory (e.g., 50M clusters: 800 MB vs 3,200 MB).

**Discrepancy:** Benchmarks run on Linux reported total solver process memory as equal
between hash-only and flat cache variants, contradicting the 4× theoretical difference.

**Resolution:** The apparent discrepancy was due to human error in running experiments.
Investigation of the issue led to separate optimisations (see KI-14: descriptor store
dual-role fix).

---

## KI-14. Descriptor State Tracked Only Inside `compact_state payload`

**Status:** RESOLVED — commits 7e73ab1, c4dc519, 76aaa43 (branch `fix/variant-build-hash-only`, merged to `dev`)
**Plan:** `docs/fix-variant-build-hash-only/implementation_plan.md`
**Impact:** Code clarity — `payload` served a dual role: (1) cache key stored in `flat_cache`,
and (2) internal tracking store for old descriptor values needed to compute Zobrist XOR deltas

The 52 card descriptors (and foundation/hole/waste header fields) used to compute the
incremental Zobrist hash had no storage of their own — they lived exclusively inside
`compact_state payload`. Every `update_*` helper read the old value via `payload.get_*()`
before XOR-ing, then wrote the new value via `payload.set_*()`. This meant `payload` had
to be maintained even in `hash_only_cache` mode, where the cache itself never stores or
reads the payload.

**Fix:** `card_descriptor` enum extracted to `descriptor.h` (commit 7e73ab1). New
`hash_descriptor_store` (plain byte arrays, 58 bytes) introduced as the old-value store
for the hash-only path (commit c4dc519). `game_state` now uses `hash_descriptor_store
hash_desc` instead of `compact_state payload` when compiled with `SOLVITAIRE_HASH_ONLY`,
and `compact_state.h` is excluded entirely from that compilation unit (commit 76aaa43).
All four `update_*` helpers and `make_move` dispatch via `#ifdef SOLVITAIRE_HASH_ONLY`.

---

## KI-15. `dual_cache` and Test Construction Always Enable Both Policy Flags

**Status:** RESOLVED by templated dispatch (Commits 3a + 3b on `feature/templated-dispatch`)
**Impact:** Was: `DualCacheTest` game_states computed hash and payload even for LRU games

The runtime `computing_flat_hash`/`computing_flat_payload` flags and `cache_type`
constructor parameter have been removed. `game_state_impl<Policy>` uses compile-time
Policy traits instead. Unit tests use `game_state` typedef (→ `FlatPolicy`) and always
compute hash/payload, which is correct for test purposes. Dual-cache test infrastructure
itself was redesigned — see KI-18 in `known-issues.md`.

---

## KI-16. FreeCell Seed 1 Flat-Only Hits Investigation

**Investigation:** Recovered 100+ `lru=MISS, flat=HIT` mismatches at op 221+ from previous conversation
**Status:** RESOLVED — Not a bug; legitimate behavior confirmed
**Impact:** None — no correctness issue; safe for Phase 1 implementation

During investigation of cache correctness, previous test run (MismatchAnalyzer.FreeCellSeed1)
reported hundreds of flat-only hits starting at operation 221. Investigation confirmed:

1. **Root cause:** Pile ordering differences between LRU and flat cache
   - When `force_lru=false`: Empty tableau pile order is NOT canonicalized
   - LRU treats different pile orders as different states (different hash)
   - Flat cache treats identical payloads as identical (pile order irrelevant)
   - Result: Flat cache finds duplicates LRU misses → `flat=HIT, lru=MISS`

2. **Verification:**
   - Mismatches only appear with `force_lru=false` (MismatchAnalyzer test)
   - Zero mismatches with `force_lru=true` (DualCacheTest) ✓
   - Behavior matches documented fix in `human_contributions.md` section 22
   - All states are genuinely identical except for tableau pile order

3. **Conclusion:** Legitimate deduplication, not a false positive or regression

**Details:** See `docs/investigation/INVESTIGATION_COMPLETE.md` for full analysis.
Investigation commit: `4c4b022`

---

## Earlier Resolved Bugs (pre-KI numbering)

The following bugs were fixed before the KI numbering scheme was introduced.
Full details are in their individual files in this directory.

| Bug | Fix commit | Details |
|---|---|---|
| STARTING(0) not position-canonical (false negatives) | `52b8b63` | `bug_starting_descriptor_not_position_canonical.md` |
| ROOT descriptor overloaded (false positives) | `52b8b63` | `bug_root_descriptor_false_positives.md` |
| Waste pointer stale on regular moves (FortunesFavor) | `52b8b63` | `bug_waste_ptr_and_canfield_wrapping.md` |
| Canfield wrapping builds not recognised (CanfieldStrict) | `52b8b63` | `bug_waste_ptr_and_canfield_wrapping.md` |
| `sol_rules` uninitialized bools (UBSan) | `cb9d26d` | `implementation_plan_v4.md` §M5 |
| `recompute_payload_from_scratch()` four bugs | `3d5f66d` | `implementation_plan_v4.md` §M5 |
| `--force-lru` pile ordering not restored in M6 Phase 1 | `a7f3744` | `implementation_plan_v4.md` §M6 |
| FreeCell seed 1 flat-only hits (op 221+) | `4c4b022` (investigation, not a bug) | `docs/investigation/INVESTIGATION_COMPLETE.md` |
| Build script omits variant binaries (KI-11) | `0653486` | `docs/fix-variant-build-hash-only/implementation_plan.md` |
| `compact_state payload` dual-role in hash-only path (KI-14) | `7e73ab1`, `c4dc519`, `76aaa43` | `docs/fix-variant-build-hash-only/implementation_plan.md` |
| JSON deal round-trip changes node counts (KI-1) | `10c233c` | `original_tableau_piles` fix |
