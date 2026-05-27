# Stage 2 Evaluation — Request for Critical Review

This document is a prompt for an Opus model to critically evaluate the Stage 2
multiplicity-encoding implementation (suit-symmetry canonicalisation). The implementing
agent (Sonnet) wrote the code; Opus evaluates correctness.

---

## What Stage 2 Was Supposed to Do

> Extend Stage 1 (from-scratch multiplicity hash, no symmetry) to handle suit-symmetry
> canonicalisation. Cards in the same static equivalence class (e.g. same-rank cards of
> same colour) must produce identical cache payloads when their board positions are
> permuted. Still from-scratch (no incremental updates). Expose via the existing
> `--cache-type multiplicity` with `--streamliners suit-symmetry`.

---

## Evaluation Checklist

### 1. Static Class Structure

- [ ] `multiplicity_static_class.h` exists with `symmetry_mode` enum and `static_class_structure`
- [ ] `determine_symmetry_mode()` logic matches `global_cache.cpp:119–133`:
  - `rules.hole` → SUIT_IRRELEVANT
  - `build_pol == RED_BLACK` → COLOUR
  - `build_pol == SAME_SUIT` → NONE (no symmetry possible)
  - Otherwise → SUIT_IRRELEVANT
- [ ] COLOUR mapping: suits {0,2} (black) share class; suits {1,3} (red) share class
  - Class = `(rank-1)*2 + colour` where colour = 0 for black, 1 for red
  - Verify: Ace of Clubs (card_id 0, suit 0) and Ace of Spades (card_id 26, suit 2) share class 0
  - Verify: Ace of Hearts (card_id 13, suit 1) and Ace of Diamonds (card_id 39, suit 3) share class 1
- [ ] SUIT_IRRELEVANT mapping: class = rank-1 (card_id % 13)
  - Verify: all four Aces (ids 0, 13, 26, 39) share class 0
- [ ] `class_start` correctly assigns contiguous payload ranges

### 2. Fixpoint Algorithm

- [ ] Raw slot bytes computed correctly for both locative and predecessor descriptors
- [ ] Sort is by slot byte ascending, with card_id tiebreak for determinism
- [ ] Canonical positions assigned = class_start + index within sorted class
- [ ] Predecessor slot bytes recomputed after each sort pass using updated canonical positions
- [ ] Loop terminates when no slot byte changes
- [ ] Assertion on max iterations (≤ 12)
- [ ] **Key correctness property:** For NONE mode (class_size == 1), the algorithm reduces
  to `canonical_pos[c] = c` (identity) — Stage 1 behaviour exactly

### 3. Scheme A (Dynamic Class Resolution)

- [ ] After fixpoint, members with equal slot bytes within a static class form a dynamic class
- [ ] All predecessor references to members of a dynamic class resolve to the *lowest*
  canonical position in that class
- [ ] This is applied correctly — it's the REFERENCING cards whose slot bytes change, not
  the dynamic class members themselves
- [ ] Final sort + position assignment is done after Scheme A resolution

### 4. Additive Hash Combining

- [ ] Formula: `H = XOR_{C} ( SUM_{m in C} Z_lookup(class_id, d_m) )`
- [ ] `SUM` uses wrapping uint64_t addition (mod 2^64), NOT overflow-checked addition
- [ ] `XOR` across classes
- [ ] Zobrist table indexed by `class_id` (0..n_classes-1), NOT card_id
- [ ] The NOT trick for face-down predecessors is preserved
- [ ] **Key correctness property:** Two class members with identical descriptors contribute
  `2 * Z[class][col]` (not 0 as XOR would give)

### 5. Payload Layout

- [ ] Payload positions are contiguous per class: class C occupies positions
  `[class_start[C], class_start[C] + class_size)`
- [ ] Within each class, members are sorted by slot byte (ascending)
- [ ] `store.set_slot(position, slot_byte)` writes to the correct payload offset
- [ ] Total payload still uses bytes 3–54 (52 slots), unchanged from Stage 1

### 6. No-Symmetry Fast Path

- [ ] When `symmetry_mode == NONE`, the fixpoint is trivially satisfied in one pass
- [ ] An early-return or short-circuit path exists for class_size == 1
- [ ] **Verify by testing:** `--cache-type multiplicity` without streamliners produces
  identical `states_searched` to Stage 1 (no regression)

### 7. Dispatch Changes

- [ ] `cache_interface.h`: `use_multiplicity_cache()` no longer rejects suit-symmetry games
- [ ] `main.cpp`: auto-dispatch sends suit-symmetric multiplicity-eligible games to
  MultiplicityPolicy instead of LRUPolicy
- [ ] `descriptor_context` includes `stream_opts`
- [ ] Engine receives symmetry mode at construction and initialises class structure

### 8. Waste-Deal Symmetry Preservation

- [ ] The `recompute_all()` waste-deal symmetry logic (stock_redeal + waste.size() %
  deal_count == 0 → all get MLD_IN_STOCK) is unchanged
- [ ] Under suit-symmetry, stock/waste cards with identical locative descriptors (MLD_IN_STOCK)
  form a single dynamic class and collapse correctly

---

## Testing to Perform

### Gate 1: Standard test suite
```bash
python3 scripts/run_tests.py
```
All 3 gates must pass.

### Gate 2: No-symmetry regression (20 seeds)
```bash
for seed in $(seq 1 20); do
    ./cmake-build-release/bin/solvitaire --type klondike --random $seed \
        --cache-type multiplicity --json 2>/dev/null | jq '.states_searched'
done
```
Compare against same seeds with `--cache-type auto`. Must be identical on 0-eviction seeds.

### Gate 3: Symmetric validation (20 seeds)
```bash
for seed in $(seq 1 20); do
    echo "=== Seed $seed ==="
    echo -n "multiplicity: "
    ./cmake-build-release/bin/solvitaire --type klondike --random $seed \
        --streamliners suit-symmetry --cache-type multiplicity --json 2>/dev/null \
        | jq '{outcome: .outcome, states: .states_searched}'
    echo -n "lru (auto):   "
    ./cmake-build-release/bin/solvitaire --type klondike --random $seed \
        --streamliners suit-symmetry --cache-type auto --json 2>/dev/null \
        | jq '{outcome: .outcome, states: .states_searched}'
done
```
- **Solvability must match on every seed** (CRITICAL)
- `states_searched` for multiplicity should generally be ≤ LRU (more cache hits from
  symmetry in flat cache), but may be higher if eviction differences dominate
- Any solvability disagreement is a critical bug — stop and investigate

### Gate 4: Spot-check other game types
```bash
# FreeCell (suit-irrelevant)
./cmake-build-release/bin/solvitaire --type free-cell --random 1 \
    --streamliners suit-symmetry --cache-type multiplicity --json

# Black Hole (hole game → suit-irrelevant)
./cmake-build-release/bin/solvitaire --type black-hole --random 1 \
    --cache-type multiplicity --json
```
Must not crash; solvability must match LRU baseline.

---

## Known Risks and What to Scrutinise

1. **Fixpoint non-convergence:** If the iteration cap is hit, that's a bug in the
   propagation logic. The v5 spec guarantees termination via downward-only distinguishability.

2. **Incorrect class mapping:** If Clubs/Spades are mapped to different classes under
   COLOUR mode, the whole thing silently produces wrong results (no crash, just wrong cache
   behaviour). Verify the mapping with a few spot-checks.

3. **Payload position layout vs Zobrist row confusion:** The payload uses canonical
   *positions* (0–51 contiguous), but the Zobrist table uses *class_id* (0–25 or 0–12)
   as the row. These are different concepts. Verify the engine doesn't confuse them.

4. **Dynamic class resolution modifying the wrong cards:** Scheme A changes the slot bytes
   of cards whose *predecessor* is in the dynamic class, not the dynamic class members
   themselves. Getting this backwards would be a subtle bug.

5. **Tiebreak determinism:** If the sort doesn't have a stable tiebreak (e.g. card_id),
   different runs could produce different canonical positions for equivalent states,
   breaking the cache's identity property.

---

## Questions for the Reviewer

1. Does the fixpoint converge correctly on a worked example? Pick Klondike seed 1 with
   colour symmetry, trace through 2–3 tableau cards manually.

2. Is the Scheme A dynamic-class resolution applied at the right point in the algorithm
   (after fixpoint, before final payload write)?

3. Does the no-symmetry fast path produce bit-identical results to Stage 1?

4. Are there any games where `use_multiplicity_cache()` now returns true but the game
   has properties that would break the canonicalisation (e.g. unusual tableau structure)?

5. Is the additive combining mathematically correct for the symmetry property? (Two
   permuted states should produce identical hashes.)
