# Known Issues

## 1. JSON Deal Round-Trip Changes Node Counts (`json_helper.cpp`)

**Affected file:** `src/main/input-output/input/json-parsing/json_helper.cpp`

**Status:** Fixed in `claude/quizzical-darwin`; present in `testing-infrastructure` and
upstream Solvitaire `master`.

### Description

`json_helper::print_game_state_as_json` serialises the tableau piles by iterating
`gs.tableau_piles` — the *runtime-reordered* list maintained by `eval_pile_order` to
keep the largest pile first. However, `deal_parser::parse_tableau_piles` reads the piles
back into `gs.original_tableau_piles` — the *original fixed order* from construction.

When pile symmetry has reordered `tableau_piles` away from the original order, the
serialised JSON records piles in the reordered sequence, but parsing assigns those cards
back in the original positional order. The resulting game state has the same cards but
in a different internal arrangement, leading to:

- Different move-generation order
- Different transposition-table (cache) hit/miss patterns
- Different `states_searched` counts — even though the deal is logically identical

**Confirmed example:** `canfield-strict` seed 4000550 with `--streamliners both`
- Run from seed: **112 275** states searched
- Run from exported JSON: **112 266** states searched (9 fewer)

### Workaround (applied for levels 2–5)

The Level 2–5 regression runner invokes the solver with `--random <seed>` directly,
bypassing JSON serialisation entirely. The oracle values were generated from seed-based
runs in the original experimental dataset, making the comparison consistent.

Level 1 uses JSON instance files, but testing showed the bug does not affect those
instances at the streamliners used (all Level 1 outcomes and node counts match the
experimental ground truth exactly).

### Fix (in `claude/quizzical-darwin`)

In `json_helper::print_game_state_as_json`, line 90:

```cpp
// BUGGY — iterates the reordered list
for (auto pr : gs.tableau_piles) {

// CORRECT — iterates the original fixed order, matching what the parser expects
for (auto pr : gs.original_tableau_piles) {
```

This one-line change is applied in the `claude/quizzical-darwin` branch and should be
merged to `testing-infrastructure` via the pending PR.

---

# Issues in `refactor-caching` Branch (Milestone 5)

The following issues were found during M5 verification and hardening (2026-03-26).
Solvability outcomes are correct; they affect node counts or internal state consistency.
Accepted as low risk for M6 proceeding.

## A. Fortunes-Favor: Waste Pointer Stale on Regular Move

**Branch:** `refactor-caching`
**Status:** Open; accepted for M6 proceeding
**Severity:** Medium (false negatives, correctness-neutral)

### Description

When `make_regular_move()` moves a card FROM the waste pile (as happens with
`auto-waste-then-stock` spaces policy), the waste pointer in the payload is not updated.
The waste pile shrinks but `compact_state.data[5]` retains the old size.

**Evidence:** `mismatch_diagnostic_fortunes-favor_seed31646033.txt` shows 18,908 LRU=HIT,
flat=MISS mismatches. At ops 13 and 22: identical board state but waste_ptr differs
(`0x12` vs `0x13`). Same descriptors, payload otherwise identical, but hashes diverge.

**Root cause:** `make_regular_move()` sets `undo.old_waste_ptr = 255` (sentinel) and never
calls `update_waste_ptr_in_hash()`. Only `make_stock_move()` (stock→waste deal) updates
the waste pointer. Regular moves from waste to tableau (auto-play) leave it stale.

**Outcome:** Still unsolvable (correct). Nodes: flat cache sees 198,928 (73% increase
from oracle 114,880) due to false negatives.

### Fix direction

In `src/main/game/search-state/game_state.cpp`, `make_regular_move()`:
1. Detect when `m.from == waste`
2. Save old waste pointer: `uint8_t old_waste_ptr = payload.get_waste_ptr()`
3. Call `update_waste_ptr_in_hash(effective_waste_ptr())`
4. Store old_waste_ptr in undo for restoration on undo

## B. Canfield-Strict: False Positives in Flat Cache

**Branch:** `refactor-caching`
**Status:** Open; root cause not fully identified
**Severity:** High (false positives are dangerous)

### Description

Flat cache produces 87 LRU=MISS, flat=HIT divergences at seed 4000100. Different board
states have identical payloads and identical hashes, causing flat cache to report a
match when the states are genuinely different.

**Evidence:** `mismatch_diagnostic_canfield-strict_seed4000100.txt`. Op 674 (flat=HIT)
has no matching board in prior ops. But ops 418 and 673 have matching payloads (bytes
3-31 identical) with different board states. KS is in different piles; AD and AH swap
between ROOT and IN_SPACE descriptors. Yet payloads are identical.

**Root cause:** Not yet identified. Needs deeper investigation — per-card descriptor
dump at both ops to confirm why descriptors that should differ produce identical payloads.
Possible involvement of reserve encoding or a descriptor assignment bug not yet caught.

**Outcome:** Still unsolvable (correct). Nodes: flat cache sees 155,109 (oracle predicts
same or fewer; actual count not captured in diagnostic).

### Next steps

1. Dump per-card descriptors for all 52 cards at ops 418, 673, and 674
2. Identify which cards have differing descriptors between the states
3. Trace those card movements through the DFS
4. Identify why the payloads are identical despite different descriptors
5. May involve reserve-card encoding or a subtle bug in descriptor assignment

## C. Recompute Payload From Scratch: Cannot Distinguish ROOT from IN_SPACE

**Branch:** `refactor-caching`
**Status:** Parked; non-blocking for M6
**Severity:** Low (debug assertion not yet wired; no user-facing impact)

### Description

The debug assertion `recompute_payload_from_scratch()` is meant to verify that the
incrementally-maintained payload matches a fresh computation from board state. However,
without move history, it cannot distinguish whether a card should have descriptor
ROOT(2) or IN_SPACE(9) when it appears at the bottom of a non-empty pile.

**Example:** A card sitting on a non-legal-build parent (fallback) has ROOT. But a card
at the bottom of a pile with face-down cards below also appears "at the bottom" from the
face-up side. The recomputation logic cannot tell which case applies without examining
the move sequence.

**Workaround:** The assertion is not wired up (not called from any path). M5 testing
proceeded without it.

### Fix direction

Deferred to a later milestone. Options:
1. Pass move history to recomputation
2. Refactor to compute both cases and accept multiple valid encodings
3. Accept that the assertion cannot be perfectly implemented and focus on other
   correctness checks

---

# Delayed M5 Tests

The following tests would ideally be part of M5 but were deferred due to time/complexity.
They should be completed before M6 is merged to `master`.

## T1. AddressSanitizer and UBSanitizer Runs

**Status:** Deferred
**Priority:** Medium
**Why delayed:** Would add 30+ minutes to test time; can be run in parallel

**What to do:**
```bash
# Build with sanitizers enabled (modify CMakeLists.txt or build.sh)
./build.sh --release --sanitizers

# Run full Level 2 regression
ctest -R regression_level2 --output-on-failure
```

**Expected:** No sanitizer warnings. If any appear, investigate before M6.

## T2. Investigate and Fix Canfield-Strict False Positives

**Status:** Blocked; needs investigation
**Priority:** High
**Why delayed:** Root cause not identified; requires deep debugging

**What to do:**
1. Use mismatch_diagnostic for seeds 4000100 (known) and a few others (e.g. 4000500)
2. Dump per-card descriptors and move trace
3. Identify the pattern — is it reserve-related? A descriptor assignment bug?
4. Fix and re-run Level 2 regression

**Expected:** Zero flat false positives. If fix requires changes to descriptor logic,
all agreement tests must be re-run.

## T3. Investigate and Fix Fortunes-Favor Waste Pointer Bug

**Status:** Clear root cause; implementation blocked
**Priority:** High
**Why delayed:** Simple fix but needs careful testing to avoid regressions

**What to do:**
1. Apply the waste pointer fix in `make_regular_move()`
2. Re-run mismatch_diagnostic at seed 31646033
3. Expect zero divergences
4. Re-run Level 2 regression for fortunes-favor

**Expected:** Zero LRU/flat divergences. Node counts should match between caches.

## T4. Large Cache Test

**Status:** Not started
**Priority:** Low
**Why delayed:** Requires multi-hour runs

**What to do:**
```bash
./cmake-build-release/solvitaire --type klondike --random 1000 \
  --cache-capacity 1000000000 --timeout 3600000  # 1 hour timeout
```

Run for 50+ seeds of 2–3 game types. Verify that large caches don't cause memory issues
or unexpected behaviour.

**Expected:** No crashes, deterministic results, reasonable memory usage.

## T5. Pre-filled Foundations and Removable Foundations

**Status:** Not started
**Priority:** Low
**Why delayed:** Affects few game types; good to verify but not blocking

**What to do:**
Find game types with `foundations_initial_cards != all` and `foundations_removable = true`.
Run Level 1 seeds with these games. Verify payload encoding is correct for pre-filled
and removable foundations.

**Expected:** Solvability matches oracle.

## T6. Stock Redeal Games

**Status:** Partially tested
**Priority:** Medium
**Why delayed:** waste_deal_symmetry was added late; Level 2 doesn't fully exercise it

**What to do:**
1. Run explicit tests for games with `stock_redeal = true` and
   `waste_size % deal_count == 0` states
2. Verify `effective_waste_ptr()` is called correctly on every stock move
3. Run a few seeds of canfield, klondike-by-threes, etc.

**Expected:** Node counts match between flat and `--force-lru` runs.

## T7. Two-Deck Games (Future Consideration)

**Status:** Not applicable to M5
**Priority:** Future (M6+ extension)

Currently excluded by `use_new_cache()`. If flat cache is extended to two-deck games,
this test becomes priority.

**What to do:**
Set `!two_decks` check to true and run agreement tests for two-deck games.

**Expected:** All outcomes match before considering flat cache for two-deck games.
