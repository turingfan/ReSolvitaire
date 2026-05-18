# Stage 2C — Testing Plan for Suit-Symmetry Canonicalisation

**Prerequisite:** Stage 2B complete (in_space(k) implemented, basic smoke tests pass).

This document describes the testing needed to validate Stage 2 before proceeding to
Stage 3 (incremental computation specification). The tests cover three levels:

1. **Unit tests** — direct algebraic validation of the canonicalisation engine
2. **Solvability cross-check** — solver-level correctness across game types and seeds
3. **Asymmetric criterion check** — trace-level validation that symmetry only adds hits

---

## Level 1: Unit Tests

New file: `src/test/unit_tests/multiplicity_canonicalisation_test.cpp`

These test the `multiplicity_descriptor_engine` directly, without running the solver.
They exercise the fixpoint, sort, additive combining, and Scheme A resolution.

### Test 1: Suit-Swap Produces Identical Hash and Payload (COLOUR mode)

The most important test. Validates that two states differing only by a suit permutation
within a colour class produce identical hash_value and identical payload (memcmp).

**Method:**
1. Create `game_state_impl<MultiplicityPolicy>` from Klondike seed 1 with
   `streamliner_options::SUIT_SYMMETRY`
2. Make a few moves to create a non-trivial tableau state
3. Record `gs.get_hash()` and `gs.get_payload()` (memcmp bytes 3-54)
4. Create a second game state from a JSON deal that is the suit-permuted version:
   swap all Clubs↔Spades (same colour, black) throughout the deal
5. Make the *same sequence of moves* (with cards suit-permuted)
6. Record hash and payload
7. `EXPECT_EQ` on hash; `EXPECT_EQ` on memcmp of payload bytes 3-54

**Complication:** constructing the suit-permuted deal. Options:
- (a) Write a helper that takes a JSON deal string and swaps suit letters
- (b) Build the state programmatically by manipulating piles directly
- (c) Test the engine in isolation: set up `descriptors[]` manually, call
  `recompute_from_descriptors()`, then set up the permuted descriptors, call again,
  compare

Option (c) is cleanest and tests the algorithm in isolation:

```cpp
TEST(MultiplicityCanonicalisationTest, ColourSwapProducesIdenticalHash) {
    // Set up engine in COLOUR mode
    multiplicity_descriptor_engine eng;
    eng.classes.init(symmetry_mode::COLOUR);

    // Scenario: Ace of Clubs (cid 0) at pile bottom (IN_SPACE),
    //           2 of Hearts (cid 14) sits on Ace of Clubs (predecessor)
    // Permuted: Ace of Spades (cid 26) at pile bottom,
    //           2 of Hearts (cid 14) sits on Ace of Spades
    // Under COLOUR sym, Clubs(0) and Spades(26) are in the same class.

    // State A: set descriptors
    for (int i = 0; i < 52; i++)
        eng.descriptors[i] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    eng.descriptors[0]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE);  // AC
    eng.descriptors[14] = multiplicity_descriptor::make_predecessor(0, false);    // 2H on AC
    eng.descriptors[26] = multiplicity_descriptor::make_locative(MLD_IN_CELL);   // AS
    eng.recompute_from_descriptors();  // NOTE: needs to be public or friend
    uint64_t hash_a = eng.hash_value;
    multiplicity_descriptor_store store_a = eng.store;

    // State B: swap AC(0) and AS(26) — same colour class
    for (int i = 0; i < 52; i++)
        eng.descriptors[i] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    eng.descriptors[26] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);  // AS
    eng.descriptors[14] = multiplicity_descriptor::make_predecessor(26, false);   // 2H on AS
    eng.descriptors[0]  = multiplicity_descriptor::make_locative(MLD_IN_CELL);   // AC
    eng.recompute_from_descriptors();
    uint64_t hash_b = eng.hash_value;

    EXPECT_EQ(hash_a, hash_b);
    EXPECT_TRUE(store_a.matches(eng.store));
}
```

**Note:** `recompute_from_descriptors()` is currently private. Either:
- Make it public (it's already called from the public `recompute_hash()`)
- Add `friend class` for the test
- Call `recompute_hash(dummy_rules)` instead (which calls `recompute_from_descriptors()`)

### Test 2: Non-Equivalent States Have Different Hash (COLOUR mode)

Same setup but with a structurally different state (e.g. 2H on AC vs 2H on 2C).
Must produce different hash and/or different payload.

### Test 3: NONE Mode Preserves Stage 1 Behaviour

Set up engine with `symmetry_mode::NONE`. Verify `canonical_pos[c] == c` for all c.
Verify hash equals a manually computed XOR of Z[c][col] values.

### Test 4: SUIT_IRRELEVANT Mode (4-member classes)

Permute all 4 suits of the same rank. Verify identical hash/payload.

### Test 5: Fixpoint Convergence

Set up a scenario requiring >1 fixpoint iteration (card A sits on card B, card B sits on
card C, and A and C are in the same static class — a predecessor chain through the class).
Verify convergence and correct result.

### Test 6: Scheme A Predecessor Collapsing

Two members of a static class have the same descriptor (same dynamic class). A third card
sits on one of them. Verify the third card's slot byte uses the lowest canonical position,
regardless of which member it actually sits on.

### Test 7: in_space(k) Pile-Indexed (TABLEAU_PILES games)

Set up two cards at bottoms of different piles with pile-indexed descriptors (e.g.
MLD_IN_SPACE+0 and MLD_IN_SPACE+1). Verify they produce different slot bytes and
different hash contributions.

### Test 8: in_space(k) vs in_space (pile-symmetric games)

Same two pile-bottom cards but with bare MLD_IN_SPACE. Verify that swapping them gives
identical hash/payload (pile-order dedup).

---

## Level 2: Solvability Cross-Check

Run the solver with both multiplicity and LRU caches, compare solvability outcomes.
This catches correctness bugs that unit tests might miss (complex game-state interactions,
specific card configurations, etc.)

### Games to test

| Game | Symmetry mode | Cache comparison |
|---|---|---|
| klondike | COLOUR | multiplicity+suit-sym vs auto (LRU) |
| free-cell | SUIT_IRRELEVANT | multiplicity+suit-sym vs auto (LRU) |
| east-haven | COLOUR (TABLEAU_PILES) | multiplicity vs auto (LRU) |
| spiderette | COLOUR (TABLEAU_PILES) | multiplicity vs auto (LRU) |
| black-hole | SUIT_IRRELEVANT (hole) | multiplicity vs auto (LRU) |

### Method

```bash
for game in klondike free-cell; do
    for seed in $(seq 1 20); do
        mult=$(./cmake-build-release/bin/solvitaire --type "$game" --random $seed \
            --streamliners suit-symmetry --cache-type multiplicity \
            --timeout 30000 --json 2>/dev/null | jq -r '.result')
        lru=$(./cmake-build-release/bin/solvitaire --type "$game" --random $seed \
            --streamliners suit-symmetry --cache-type auto \
            --timeout 30000 --json 2>/dev/null | jq -r '.result')
        if [ "$mult" != "$lru" ]; then
            echo "MISMATCH: $game seed $seed: mult=$mult lru=$lru"
        fi
    done
done
```

For TABLEAU_PILES games (no streamliner needed — they don't need suit-sym to benefit):
```bash
for game in east-haven spiderette will-o-the-wisp; do
    for seed in $(seq 1 10); do
        mult=$(./cmake-build-release/bin/solvitaire --type "$game" --random $seed \
            --cache-type multiplicity --timeout 30000 --json 2>/dev/null | jq -r '.result')
        lru=$(./cmake-build-release/bin/solvitaire --type "$game" --random $seed \
            --cache-type auto --timeout 30000 --json 2>/dev/null | jq -r '.result')
        if [ "$mult" != "$lru" ]; then
            echo "MISMATCH: $game seed $seed: mult=$mult lru=$lru"
        fi
    done
done
```

**Pass criterion:** zero solvability mismatches.

### No-symmetry regression

Re-confirm Stage 1 criterion: Klondike seeds 1-20 with `--cache-type multiplicity`
(no streamliner) must match `--cache-type auto` on `states_searched` for 0-eviction seeds.

---

## Level 3: Asymmetric Criterion (Trace-Based)

This is the gold-standard validation from v5 spec §10. For symmetric games:
- Every cache HIT in the old cache (LRU) must also be a HIT in the new cache (multiplicity)
- Additional HITs in the multiplicity cache are expected (symmetry-detected equivalences)
- Any old-HIT → new-MISS is a canonicalisation bug

### Challenge

The two caches explore different search trees (different hit sets → different move
ordering → different DFS paths). A direct trace comparison doesn't work because the
operation sequences diverge. We can't pair up individual operations.

### Proposed approach: hash-set comparison

Instead of comparing traces line-by-line, extract the *set of state hashes* that
received a HIT in each run. Then check: `lru_hit_hashes ⊆ multiplicity_hit_hashes`.

**Problem:** the two caches use different hash functions (flat Zobrist vs multiplicity
Zobrist). LRU doesn't use Zobrist at all — it uses pile-order canonicalised state keys.
So we can't directly compare hash values across cache types.

### Alternative: same-cache-type comparison

Run the multiplicity cache twice on the same seed:
- Run A: `--streamliners none --cache-type multiplicity` (NONE mode, no symmetry)
- Run B: `--streamliners suit-symmetry --cache-type multiplicity` (COLOUR/SI mode)

Both use the multiplicity Zobrist hash. In Run B, symmetric states that were distinct
in Run A now hash identically. So:
- Every state that Run A finds in the cache, Run B should also find (since Run B's
  canonical hash is a coarsening of Run A's hash)
- Run B may find additional hits (symmetry-detected equivalences)

**But:** this comparison also has different search trees because the symmetry streamliner
changes move generation (auto-foundation moves may differ). The streamliner affects
solvability independently of the cache.

### Practical recommendation

Given the difficulty of trace-level asymmetric validation across different search trees,
the most practical Level 3 test is:

1. **Unit tests (Level 1)** provide the direct algebraic guarantee that suit-permuted
   states canonicalise identically. This is the primary correctness assurance.

2. **Solvability cross-check (Level 2)** catches any bugs that cause wrong verdicts.

3. **For trace-level confidence:** run a few seeds with `--cache-type multiplicity
   --streamliners suit-symmetry` in trace mode. Manually inspect a handful of HIT events
   to verify the hit state is genuinely a suit-symmetric duplicate of a previously inserted
   state. This is a spot-check, not a systematic criterion.

4. **If a systematic asymmetric check is desired:** implement a test-only mode that runs
   the multiplicity cache in NONE mode and records all inserted state payloads. Then re-run
   in COLOUR mode: for each HIT, verify the hit payload, when de-canonicalised, matches
   one of the NONE-mode payloads. This requires non-trivial tooling and is best deferred
   to after Stage 2 is otherwise validated.

---

## Implementation Order

1. Write unit tests (Level 1, tests 1-8)
2. Run solvability cross-check (Level 2) — scripted, no new code needed
3. Confirm no-symmetry regression (Level 2) — re-run Stage 1 validation
4. Spot-check a few trace HITs manually (Level 3, lightweight)
5. Declare Stage 2 validated; proceed to Stage 3

---

## Who Does What

- **Unit tests (Level 1):** Sonnet implements, Opus reviews
- **Solvability cross-check (Level 2):** run by whichever agent is active after unit tests
- **Trace spot-check (Level 3):** Opus (manual inspection)

---

## Relationship to Auto-Dispatch

The auto-dispatch change (routing suit-sym games to MultiplicityPolicy by default) is
**not part of testing**. It is a production deployment decision that happens after testing
validates correctness. When it happens:

1. Update regression oracles to reflect multiplicity as the default for eligible games
2. Change auto-dispatch in main.cpp, benchmark.cpp, solvability_calc.cpp
3. Re-run all regression levels to build new oracles

This is a separate task from Stage 2C testing.
