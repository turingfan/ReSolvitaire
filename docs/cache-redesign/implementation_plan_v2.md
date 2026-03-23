# ReSolvitaire Cache Refactor: Revised Implementation Plan

**Version:** 2.0  
**Date:** 22 March 2026  
**Authors:** Ian Gent & AI Assistant  
**Reference Documents:**
- *ReSolvitaire Cache Redesign: Architecture and Payload Specification* (main.pdf)
- *Descriptor-Aligned Zobrist Hashing for ReSolvitaire* (descriptor_zobrist.pdf)
- *Implementation Plan v1.0* (implementation_plan.md) — Milestones 0 and 1 completed

---

## Status

Milestones 0 (baseline) and 1 (abstract cache interface) from the original plan are **completed**.
Milestone 2 from the original plan (pile-role-position Zobrist hash) has been **superseded** by the descriptor-aligned Zobrist design described in `descriptor_zobrist.pdf`.

This revised plan replaces the original Milestones 2–9 with a new sequence starting from Milestone 2.

## Key Design Change: Descriptor-Aligned Zobrist Hashing

The original plan maintained the Zobrist hash and payload as independent parallel structures. The revised design aligns the Zobrist hash directly with the payload's per-card descriptor values:

- **Zobrist key table:** `Z_card[52][16]` — one random 64-bit value per (card, descriptor) pair. Plus small tables for foundation tops, waste pointer, and hole top. Total: ~8 KB, fits in L1 cache.
- **No per-pile hashes.** No Layer 1/Layer 2 distinction. No additive combining for interchangeable piles. Symmetry invariance is automatic because descriptors never reference pile indices.
- **Unified update path:** Hash and payload are updated together — same event, same code path. One nibble write + two XOR operations per moved card.

## Descriptor Values

| Value | Name | Meaning |
|---|---|---|
| 0 | STARTING | Card in original deal position AND in original face-up/face-down state. Also used for cards on foundations (ignored in comparison). |
| 1 | STARTING_FACE_UP | Card originally dealt face-down, now revealed, but not moved from original pile position |
| 2 | ROOT | Bottom face-up card of a tableau pile, moved from starting position |
| 3 | IN_CELL | Card in a free cell |
| 4 | PARENT_0 | Built on first legal parent |
| 5 | PARENT_1 | Built on second legal parent |
| 6 | PARENT_2 | Built on third legal parent |
| 7 | PARENT_3 | Built on fourth legal parent |
| 8 | IN_HOLE | Card played to hole (hole games only) |
| 9–15 | RESERVED | Zero (future: two-deck parents, streamliners) |

**STARTING_FACE_UP transitions:**
- On reveal move: STARTING → STARTING_FACE_UP
- On subsequent move of revealed card: STARTING_FACE_UP → ROOT / PARENT_i / IN_CELL / etc.
- On undo of reveal: STARTING_FACE_UP → STARTING

---

## Milestone 2: Descriptor-Aligned Zobrist Hash and Compact Payload

**Goal:** Implement the compact_state payload and descriptor-aligned Zobrist hash. Both maintained incrementally in `make_move`/`undo_move`. Neither connected to a cache yet.

This milestone merges the original Milestones 2 and 3.

### Task 2.1: Zobrist Key Tables

Create `src/main/game/zobrist.h` and `zobrist.cpp`:

```cpp
class zobrist_hash {
public:
    static void init(uint64_t seed = 0xDEADBEEF12345678ULL);
    
    // Per-card descriptor keys
    static uint64_t card_key(uint8_t card_id, uint8_t descriptor);
    
    // Foundation top rank keys (foundation games)
    static uint64_t foundation_key(uint8_t suit, uint8_t rank);
    
    // Waste pointer keys
    static uint64_t waste_key(uint8_t ptr);
    
    // Hole top card keys (hole games)
    static uint64_t hole_top_key(uint8_t card_id);
    
    // Card ID from suit and rank
    static uint8_t card_id(uint8_t suit, uint8_t rank);
    
private:
    static uint64_t Z_card[52][16];     // 832 entries, ~6.5 KB
    static uint64_t Z_found[4][14];     // 56 entries
    static uint64_t Z_waste[64];        // 64 entries
    static uint64_t Z_hole_top[52];     // 52 entries
    static bool initialised;
};
```

- `init()`: fill all tables using `std::mt19937_64` with the fixed seed
- `card_key(c, d)`: return `Z_card[c][d]`
- Other accessors: direct array lookup
- `card_id(suit, rank)`: return `suit * 13 + (rank - 1)`

**If the existing Milestone 2 Zobrist files exist from the previous implementation attempt, replace them entirely.** The new design has a fundamentally different table structure (card × descriptor, not card × role × position).

### Task 2.2: Compact State Payload

Create `src/main/game/compact_state.h` and `compact_state.cpp`:

```cpp
struct compact_state {
    uint8_t data[32];
    
    enum descriptor : uint8_t {
        STARTING         = 0,
        STARTING_FACE_UP = 1,
        ROOT             = 2,
        IN_CELL          = 3,
        PARENT_0         = 4,
        PARENT_1         = 5,
        PARENT_2         = 6,
        PARENT_3         = 7,
        IN_HOLE          = 8,
    };
    
    void clear();
    
    // Occupied flag (byte 0: 0 = empty slot, nonzero = occupied)
    void set_occupied(bool occ);
    bool is_occupied() const;
    
    // Depth counter (bytes 1-2, excluded from comparison)
    void set_depth(uint16_t d);
    uint16_t get_depth() const;
    
    // Foundation top ranks (bytes 3-4) / hole-top (byte 3, low 6 bits)
    void set_foundation(uint8_t suit, uint8_t rank);
    uint8_t get_foundation(uint8_t suit) const;
    void set_hole_top(uint8_t card_id);
    uint8_t get_hole_top() const;
    
    // Waste pointer (byte 5)
    void set_waste_ptr(uint8_t ptr);
    uint8_t get_waste_ptr() const;
    
    // Per-card descriptor (bytes 6-31, 4 bits per card)
    void set_descriptor(uint8_t card_id, uint8_t value);
    uint8_t get_descriptor(uint8_t card_id) const;
    
    // Compare payloads (bytes 3-31 only, excluding occupied flag and depth)
    bool matches(const compact_state& other) const;
};
```

**Payload byte layout:**
```
Byte 0:     Occupied flag (0 = empty slot, nonzero = occupied)
Bytes 1-2:  Depth (16-bit unsigned)
Bytes 3-4:  Foundation Clubs|Hearts nibbles  OR  hole-top (6 bits + padding)
Byte 5:     Waste pointer (0-63)
Bytes 6-31: Card descriptors — byte 6 has card 0 (low) | card 1 (high), etc.
```

**Nibble access** (4-bit descriptors, two per byte, never spanning byte boundaries):
```cpp
// Read descriptor for card c
uint8_t byte_idx = 6 + (c / 2);
uint8_t value = (c % 2 == 0) ? (data[byte_idx] & 0x0F) 
                               : (data[byte_idx] >> 4);

// Write descriptor for card c
if (c % 2 == 0)
    data[byte_idx] = (data[byte_idx] & 0xF0) | (new_val & 0x0F);
else
    data[byte_idx] = (data[byte_idx] & 0x0F) | (new_val << 4);
```

### Task 2.3: Parent Lookup Table

Create `src/main/game/parent_table.h` and `parent_table.cpp`:

- Given `(card_id, build_policy)` → list of parent card IDs in fixed suit order
- Given `(card_id, parent_card_id, build_policy)` → descriptor value (PARENT_0 through PARENT_3)
- RED_BLACK: 2 opposite-colour parents, ordered by parent suit index
- SAME_SUIT: 1 parent (same suit, rank+1)
- ANY_SUIT: 4 parents, ordered Clubs/Hearts/Spades/Diamonds
- NO_BUILD: no parents
- Max-rank cards (kings): no parents (return empty list)

### Task 2.4: Game Selection Function

Create a helper function (e.g. in a new file or in an appropriate existing header):

```cpp
bool use_new_cache(const sol_rules& rules);
```

Returns true if the game is: single deck (`!two_decks`), no sequences (`sequence_count == 0`), no accordion (`accordion_size == 0`), no spider-type stock dealing (`stock_deal_t != TABLEAU_PILES`). Spider-type dealing distributes cards across tableau piles in a way that breaks the per-card descriptor model's pile symmetry assumptions. Otherwise returns false (use old cache).

### Task 2.5: Integrate into game_state

Add to `game_state.h`:
```cpp
uint64_t zobrist_hash_value;
compact_state payload;
```

**Initialisation** (in constructors, after the deal is complete):
1. `payload.clear()` — all descriptors = STARTING (0), all headers = 0
2. Set foundation tops in payload for any pre-filled foundations
3. Set waste pointer to initial value
4. For pre-filled cells: set those cards' descriptors to IN_CELL
5. Compute initial Zobrist hash: XOR of `Z_card[c][0]` for all 52 cards, XOR with foundation keys and waste key

**Incremental updates in make_move / undo_move:**

For each move type, the update follows the pattern:
```cpp
void update_payload_and_hash(uint8_t card_id, uint8_t old_desc, uint8_t new_desc) {
    payload.set_descriptor(card_id, new_desc);
    zobrist_hash_value ^= zobrist_hash::card_key(card_id, old_desc)
                        ^ zobrist_hash::card_key(card_id, new_desc);
}
```

**Regular move (card from pile A to pile B):**
1. Determine moved card's card_id and current descriptor (= old_desc)
2. Determine new descriptor based on destination:
   - To foundation: new_desc = STARTING (0), also update foundation field and hash
   - To cell: new_desc = IN_CELL (3)
   - To hole: new_desc = IN_HOLE (1), also update hole-top field and hash
   - To tableau on parent q: new_desc = PARENT_i (look up via parent_table)
   - To tableau empty pile (as root): new_desc = ROOT (2)
3. Call `update_payload_and_hash(card_id, old_desc, new_desc)`
4. If reveal_move: the revealed card transitions STARTING → STARTING_FACE_UP
   - Call `update_payload_and_hash(revealed_card_id, STARTING, STARTING_FACE_UP)`

**Built group move:**
1. Only the bottom card of the group changes descriptor (new parent or ROOT)
2. Other cards in the group retain their descriptors unchanged
3. If reveal_move: update revealed card as above

**Stock-to-waste deal:**
1. Update waste pointer in payload and hash
2. No card descriptors change (cards in stock/waste are all STARTING)

**Stock-k-plus move (waste card to target):**
1. Update waste pointer in payload and hash
2. Update the played card's descriptor (STARTING → PARENT_i / ROOT / etc.)
3. If flip_waste (redeal): update waste pointer again

**Stock-to-all-tableau deal:**
1. Update waste pointer
2. For each dealt card: STARTING → ROOT (if dealt to empty pile or non-matching top) or STARTING → PARENT_i (if matching build)
   - Note: in stock-to-all-tableau, cards are forcibly dealt without checking build rules. If the dealt card doesn't match the pile top, encode as ROOT.

**Undo of any move:**
- Reverse all updates. Each `update_payload_and_hash` call is self-inverse (XOR is self-inverse; nibble write restores old value from the stored old_desc).
- Store old descriptor values on the move stack (4 bits per affected card — store as part of the move or in a parallel stack).

### Task 2.6: Tests

1. **Hash initialisation test:** Create game states for FreeCell (seed 1), Klondike (seed 1), Black Hole (seed 1). Verify initial hash is non-zero and deterministic (same seed → same hash).

2. **Hash consistency test:** For each test game, make a move, undo it, verify hash returns to original value. Repeat for 100 random legal moves.

3. **Payload consistency test:** After a sequence of 50 moves and undos, compute the payload from scratch (by walking the full game state and assigning descriptors based on card positions) and compare against the incrementally maintained payload. They must match byte-for-byte.

4. **Symmetry invariance test:** Create two game states from the same seed. In one, swap two tableau piles (by making moves that result in the piles being exchanged). Verify the hashes are identical. This may be difficult to engineer directly; an alternative is to verify that the hash depends only on card descriptors, not pile indices, by checking that the hash computation uses only `Z_card[card_id][descriptor]` values.

5. **STARTING_FACE_UP test:** In a Klondike game, make a move that reveals a face-down card. Verify the revealed card's descriptor is STARTING_FACE_UP (1). Verify the hash changed. Undo; verify descriptor returns to STARTING (0) and hash returns to original.

6. **Foundation update test:** Move a card to foundations. Verify both the card's descriptor (set to 0) and the foundation field are updated correctly in both payload and hash.

7. **All existing tests still pass** with identical solvability results.

### Review Criteria
- All existing tests pass with identical results to baseline
- All new tests pass
- Zobrist table is ≤8 KB total
- `compact_state` is exactly 32 bytes
- No per-pile hash arrays exist — the hash is a flat XOR of per-card and header contributions
- Payload and hash are updated in the same code path
- STARTING_FACE_UP is correctly maintained on reveal/undo-reveal

---

## Milestone 3: Flat Cache Implementation ✓ COMPLETE

**Goal:** Implement the flat open-addressed cache with two-slot clusters. Not yet connected to the solver.
**Status:** Complete. Committed in `19ea515`.

*(This corresponds to Milestone 4 from the original plan, renumbered.)*

### Tasks

1. Create `src/main/game/flat_cache.h` and `flat_cache.cpp`:

```cpp
class flat_cache : public cache_interface {
public:
    struct alignas(64) cluster {
        compact_state entries[2];   // 2 × 32 bytes = 64 bytes = 1 cache line
    };
    
    explicit flat_cache(uint64_t max_entries);
    
    bool insert(const game_state& gs) override;
    bool contains(const game_state& gs) const override;
    void clear() override;
    uint64_t size() const override;
    uint64_t get_states_removed_from_cache() const override;
    uint64_t bucket_count() const override;

private:
    uint64_t cluster_index(uint64_t hash) const;
    bool insert_into_cluster(cluster& cl, const compact_state& state);
    
    std::vector<cluster> clusters;
    uint64_t num_clusters;
    uint64_t occupied_count;
    uint64_t eviction_count;
};
```

2. **Cluster indexing:** `cluster_index(hash)` maps the 64-bit Zobrist hash to a cluster index. Use multiply-high: `(uint64_t)((__uint128_t)hash * num_clusters >> 64)`. Fall back to modulo if `__uint128_t` is unavailable.

3. **Replacement policy (TwoBig1):**
   - Slot 0: depth-preferred. Overwrite only if new entry's depth ≤ stored depth.
   - Slot 1: always-replace.
   - On insert: check both slots for match (using `compact_state::matches`). If found → return false (already present). If not found → try slot 0, then slot 1.

4. **Empty slot detection:** An empty slot is identified by byte 0 (the occupied flag) being zero. On insertion, set byte 0 to 1. The `compact_state::is_occupied()` method checks this. This is clean and unambiguous — no need to inspect state bytes.

5. **Access pattern:** `insert(gs)` reads `gs.zobrist_hash_value` and `gs.payload`. `contains(gs)` does the same. Add public getters to `game_state` if needed: `uint64_t get_zobrist_hash() const` and `const compact_state& get_payload() const`.

### Tests

- Unit test: create flat_cache with capacity 1000. Insert 500 unique states, verify `size() == 500`. Verify `contains()` for all 500.
- Unit test: overflow the cache, verify no crash, eviction count increments.
- Unit test: verify that a state inserted and then looked up returns true for `contains`.
- Stress test: insert 100,000 random FreeCell states.

### Review Criteria
- All existing tests pass
- New flat_cache tests pass
- Benchmark output still matches baseline (flat cache exists but solver still uses old cache)

---

## Milestone 4: Wire the Solver to the New Cache ✓ COMPLETE

**Goal:** Connect the flat cache to the solver for supported game types. First milestone where behaviour changes.
**Status:** Complete. Committed in `8caf454`.

*(Corresponds to original Milestone 5.)*

### Tasks

1. In `solver.cpp` constructor: check `use_new_cache(rules)`. If true, construct `flat_cache`; if false, construct `lru_cache`.

2. Modify the DFS loop:
   - For flat_cache: `insert` returns bool. No iterator stored. No `set_non_live` calls.
   - Make `node::cache_state` optional (or use a union/variant). Skip `set_non_live` when using flat cache.
   - Add depth safety bound: abort if `res.depth` exceeds a configurable limit (default: `cache_capacity * 2`).

3. Set depth in payload before insertion: `gs.payload.set_depth(res.depth)`.

4. Update `solvability_calc.cpp` similarly.

5. **Leave pile ordering enabled for now.** It affects move generation order. Removing it is Milestone 6.

### Tests

- Run full benchmark suite. **Solvability classifications must match baseline for every seed.** States-searched counts may differ (different collision patterns, no live bits).
- Run with AddressSanitizer.
- Test with `--cache-capacity 1000` (stress eviction) and `--cache-capacity 100000000` (large cache).

### Review Criteria
- Solvability matches baseline for all benchmark seeds
- No crashes or memory errors
- New cache used for single-deck foundation/hole games without spider-type dealing
- Old cache used for Spider, Gaps, Accordion, two-deck games, and spider-deal games

---

## Milestone 5: Verification and Hardening

**Goal:** Thorough correctness testing across all supported single-deck game types.

*(Corresponds to original Milestone 6.)*

### Tasks

1. Run regression Levels 1–3 (covering ~80 game types with winnable and unwinnable instances). All outcomes must match oracles. This replaces the original plan of running 100 seeds per game type.

2. Add debug mode (`#ifndef NDEBUG`): on every cache insert, recompute payload from scratch and assert it matches the incremental payload.

3. Add debug mode: on every cache hit, verify using the old `cached_game_state` encoding that the states genuinely match.

4. Run with sanitizers (Address, UndefinedBehaviour).

5. Test edge cases: `--cache-capacity 1000`, games with pre-filled foundations, games with stock redeal, games with `foundations_removable`.

### Review Criteria
- All game types agree on solvability between old and new cache
- Debug assertions pass
- No sanitizer errors

---

## Milestone 6: Remove Pile Ordering for New-Cache Games

**Goal:** Eliminate `eval_pile_order` for games using the new cache.

*(Corresponds to original Milestone 7.)*

### Tasks

1. Add `bool use_pile_ordering` flag to `game_state`, set based on `use_new_cache(rules)`.
2. Guard `eval_pile_order` calls in `place_card`/`take_card` with this flag.
3. This changes move generation order (DFS explores moves in a different sequence) but not correctness.

### Tests
- Solvability matches for all tested games/seeds
- Measure time improvement

### Review Criteria
- Solvability matches
- Measurable performance improvement on games with many interchangeable piles

---

## Milestone 7: Performance Benchmarking and Tuning

**Goal:** Measure improvement and tune parameters.

*(Corresponds to original Milestone 8.)*

### Tasks

1. Comprehensive benchmarks: wall-clock time, states/second, peak memory, cache hit rate, eviction count.
2. Tune replacement policy: test always-replace vs TwoBig1 vs depth-only.
3. Profile hotspots (nibble access, memcmp, hash computation).
4. Consider large-page allocation for the flat array.

### Review Criteria
- Documented performance comparison
- Chosen replacement policy with justification
- Target: ≥2× more states cached in same memory, or ≥1.5× faster for hard instances

---

## Milestone 8: Documentation and Merge Preparation

**Goal:** Clean up and prepare for merge to `mac-dev`.

*(Corresponds to original Milestone 9.)*

### Tasks

1. Remove dead code, clean up TODOs
2. Add comment blocks referencing design documents
3. Organise commits for clean merge
4. Create PR with benchmark results

### Review Criteria
- Compiles with no warnings (`-Wall -Wextra`)
- All tests pass
- PR ready for review

---

## Appendix A: Files Created or Modified

### New files
| File | Milestone | Purpose |
|---|---|---|
| `src/main/game/cache_interface.h` | M1 (done) | Abstract cache interface |
| `src/main/game/zobrist.h` | M2 | Descriptor-aligned Zobrist key tables |
| `src/main/game/zobrist.cpp` | M2 | Zobrist implementation (~500 mt19937_64 calls) |
| `src/main/game/compact_state.h` | M2 | 32-byte payload struct |
| `src/main/game/compact_state.cpp` | M2 | Payload methods (nibble access, matches) |
| `src/main/game/parent_table.h` | M2 | Parent card lookup table |
| `src/main/game/parent_table.cpp` | M2 | Parent table implementation |
| `src/main/game/flat_cache.h` | M3 | Flat cache class |
| `src/main/game/flat_cache.cpp` | M3 | Flat cache implementation |

### Modified files
| File | Milestone | Change |
|---|---|---|
| `src/main/game/global_cache.h` | M1 (done) | lru_cache implements cache_interface |
| `src/main/solver/solver.h` | M1 (done), M4 | cache_interface, optional cache_state |
| `src/main/solver/solver.cpp` | M1 (done), M4 | Cache factory, conditional live-bit logic |
| `src/main/game/search-state/game_state.h` | M2 | Add zobrist_hash_value, payload |
| `src/main/game/search-state/game_state.cpp` | M2 | Unified hash+payload updates in make/undo |
| `src/main/game/search-state/game_state.pile_order.cpp` | M6 | Conditional pile ordering |
| `src/main/main.cpp` | M2, M4 | Zobrist init, cache factory |
| `src/main/evaluation/solvability_calc.cpp` | M4 | Cache factory |

### Files to replace (from previous M2 attempt)
| File | Action |
|---|---|
| `src/main/game/zobrist.h` | Replace entirely with descriptor-aligned version |
| `src/main/game/zobrist.cpp` | Replace entirely |
| Any per-pile hash members in game_state.h | Remove |

## Appendix B: Complete Descriptor Reference

| Value | Name | Transitions from | Transitions to |
|---|---|---|---|
| 0 | STARTING | (initial) | STARTING_FACE_UP (on reveal); ROOT, PARENT_i, IN_CELL, IN_HOLE (on move); set to 0 on foundation entry |
| 1 | STARTING_FACE_UP | STARTING (on reveal) | ROOT, PARENT_i, IN_CELL, IN_HOLE (on move); STARTING (via undo reveal) |
| 2 | ROOT | STARTING, STARTING_FACE_UP, PARENT_i, IN_CELL | PARENT_i, IN_CELL, IN_HOLE; STARTING or STARTING_FACE_UP (via undo) |
| 3 | IN_CELL | STARTING, STARTING_FACE_UP, ROOT, PARENT_i | ROOT, PARENT_i, IN_HOLE; STARTING or STARTING_FACE_UP (via undo) |
| 4–7 | PARENT_0–3 | STARTING, STARTING_FACE_UP, ROOT, IN_CELL | ROOT, IN_CELL, IN_HOLE; STARTING or STARTING_FACE_UP (via undo) |
| 8 | IN_HOLE | STARTING, STARTING_FACE_UP, ROOT, PARENT_i, IN_CELL | (undo only) |
| 9–15 | RESERVED | — | — |

## Appendix C: Payload Byte Layout

```
Byte 0:     Occupied flag (0 = empty slot, nonzero = occupied)
Bytes 1-2:  Depth (16-bit unsigned)
Byte 3:     Foundation Clubs (low nibble) | Foundation Hearts (high nibble)
            OR: Hole-top card ID (low 6 bits) for hole games
Byte 4:     Foundation Spades (low nibble) | Foundation Diamonds (high nibble)
            OR: zero for hole games
Byte 5:     Waste pointer (0-63; 0 if no stock)
Bytes 6-31: Card descriptors (52 × 4-bit nibbles)
            Byte 6:  card 0 (low nibble) | card 1 (high nibble)
            Byte 7:  card 2 (low nibble) | card 3 (high nibble)
            ...
            Byte 31: card 50 (low nibble) | card 51 (high nibble)
```

**Comparison:** `memcmp(payload.data + 3, other.data + 3, 29)`
