# Investigation: FreeCell Seed 1 Flat-Only Hits (LRU=MISS, Flat=HIT)

**Status:** PAUSED - needs to recover test infrastructure and re-run diagnostic
**Date Started:** 2026-04-10
**Priority:** Blocks Phase 1 implementation until resolved

---

## The Issue

In a previous conversation's test run (`MismatchAnalyzer.FreeCellSeed1`), the following pattern was discovered:

- **Game:** FreeCell
- **Seed:** 1
- **First mismatch at operation 221**
- **Pattern:** `lru=MISS, flat=HIT` (flat-only hits)
- **Count:** Hundreds of such mismatches detected (op 221, 222, 230, 231, 246, 10862, 10917, ... up to op 45554+)
- **Hash of op 221 state:** `0x1ee3eac85ffb8a78`

### What This Means

- **LRU cache says:** "This state has never been seen before" (MISS)
- **Flat cache says:** "This state WAS seen before" (HIT)

This is a **false positive in the flat cache** — the flat cache is claiming to have seen a state when it actually hasn't, or it's matching the wrong state.

### Why It Matters

This suggests one of two scenarios:

1. **False positive bug in flat cache:** Different board states are being encoded with the same payload/hash, causing incorrect deduplication
2. **Legitimate deduplication with pile-ordering artifacts:** When `skip_pile_ordering=true` (M6 behavior), swapped empty tableau piles produce different LRU hashes but identical flat payloads — these are actually the same state, not a bug

---

## Critical Hypothesis: Possible Regression

**Important:** The mismatches may represent a **semantic regression** that went undetected because:

1. **Tests were designed to allow flat-only hits:** The `run_flat_better_agreement_test()` in DualCacheTest only asserts `lru_only_hits == 0`. It explicitly ALLOWS `flat_only_hits > 0`.

2. **Previous behavior:** FreeCell seed 1 may have shown zero flat-only hits in earlier versions (before some change).

3. **Current behavior:** Fresh diagnostic shows zero hits, but previous test showed hundreds.

4. **The discrepancy:** Something changed between the two test runs. Either:
   - **Case A (Regression):** A recent commit introduced spurious flat-only hits that are NOT legitimate deduplication but rather newly-created false positives. Tests pass because we allow them.
   - **Case B (Bug fix):** A recent commit fixed something and now op 221 legitimately doesn't match anything.
   - **Case C (Test infrastructure change):** The test configuration changed (e.g., `force_lru` parameter), making direct comparison invalid.

5. **Risk:** If Case A is true, we've silently introduced a correctness bug that only manifests as increased node counts (like the fortunes-favor 73% increase), not obvious failures.

### How to Detect Regression

If op 221 in the current code still shows as `lru=MISS, flat=HIT`, trace backward:
- What board state is at op 221 now?
- Which earlier op does flat cache claim matches it?
- Did that earlier op exist in the previous version?
- Did the board states change due to move generation or state representation changes?
- Are the states genuinely equivalent or is this a new false positive?

---

## What We Know

### Previous Investigation (Session from transcript b63jir19e.txt)

The previous conversation:
- Ran a test called `MismatchAnalyzer.FreeCellSeed1`
- Detected hundreds of `lru=MISS, flat=HIT` mismatches
- Started investigating op 221 specifically
- **Status**: The test infrastructure and detailed analysis were started but not completed

### Current Investigation (2026-04-10)

When I re-ran a fresh diagnostic on FreeCell seed 1:
- Created `/tmp/freecell_flat_only_diagnostic.cpp` - a custom program to capture board states at mismatch points
- Compiled and executed it
- **Result:** Showed ZERO flat-only hits in the current code
- **Conclusion:** Something has changed between the previous test run and now

### Possible Explanations

1. **Code was fixed:** The waste_ptr bug (commit 52b8b63) or descriptor bugs (commit c93433d) may have resolved this
2. **Test configuration changed:** The `force_lru` parameter or other test setup might differ
3. **Test infrastructure changed:** The MismatchAnalyzer test used in the previous run may have been replaced or disabled

---

## Critical Information

### Previous Test Infrastructure

The previous test was called `MismatchAnalyzer.FreeCellSeed1` but I cannot locate this test class in the current codebase.

**Current test files found:**
- `src/test/unit_tests/dual_cache_test.cpp` — contains DualCacheTest with three agreement functions
- `src/test/unit_tests/mismatch_diagnostic.cpp` — contains MismatchDiagnostic tests
- `src/test/unit_tests/mismatch_analyzer.cpp` — mentioned in CMakeLists.txt line 140

**Status:** Need to locate/verify which test class produces the MismatchAnalyzer output

### Current Test Configuration

FreeCell uses `run_flat_better_agreement_test()` in DualCacheTest:
- Passes `force_lru=true` to game_state constructor
- Only asserts `lru_only_hits == 0`
- **ALLOWS** `flat_only_hits > 0`

This is intentional — flat cache can find duplicates that LRU misses when pile canonicalization is disabled.

### Known Fixes Applied

Both bugs that could cause flat-only hits were supposedly fixed:

1. **Commit 52b8b63** (2026-03-28): "fix(M5): canfield wrapping, waste-ptr stale, dual-cache test infrastructure"
   - Fixed waste_ptr not being updated in `make_regular_move()` when source is waste
   - Fixed parent_table not recognizing wrapping builds in Canfield

2. **Commit c93433d**: "fix: resolve descriptor bugs causing false negatives and false positives"
   - Fixed STARTING descriptor not being position-canonical (caused LRU=HIT, flat=MISS)
   - Fixed ROOT descriptor overloaded (caused LRU=MISS, flat=HIT false positives)

Both commits are ancestors of current HEAD.

---

## Next Steps to Resolve

### Phase 0: Investigate op 221 in Current Code (CRITICAL - May Reveal Regression)

**Goal:** Determine if op 221 still produces the mismatch or if it's been genuinely fixed.

1. **Add detailed logging to op 221 in FreeCell seed 1**
   - Modify mismatch_diagnostic.cpp or create new recording cache
   - At op 221 specifically, capture:
     - Full board state (all piles)
     - All 52 card descriptors
     - Hash value
     - Zobrist hash decomposition (which keys contribute to the hash)

2. **Search backward for earlier op with matching payload**
   - If op 221 still has flat=HIT but lru=MISS, which earlier operation does flat cache match it to?
   - Extract that earlier op's board and descriptors

3. **Determine if it's a regression or legitimate**
   - Compare the two board states:
     - If boards are identical: Legitimate deduplication (but why did LRU miss it?)
     - If boards differ: False positive — this is a semantic regression
   - Check if the earlier op's descriptor pattern changed due to recent commits
   - Use `git log --oneline -p -- src/main/game/search-state/game_state.cpp` to see descriptor assignment changes

4. **If regression confirmed:**
   - Identify which commit introduced it
   - Understand the mechanism (descriptor assignment? hash computation? state generation?)
   - Decide: Fix before Phase 1, or document as blocker?

---

### Phase 1: Recover the Test Infrastructure (PRIORITY if Phase 0 shows no regression)

1. **Locate the MismatchAnalyzer test class**
   - Search for `mismatch_analyzer.cpp` in CMakeLists.txt line 140
   - Verify if it's still being compiled and linked
   - Check what test it produces

2. **Run the original test to reproduce the issue**
   - Execute `./cmake-build-release/bin/unit_tests --gtest_filter="MismatchAnalyzer.*"`
   - Capture output to understand if mismatches still occur or if they've been fixed

3. **If mismatches still exist:**
   - Note which seed/operation shows them
   - Proceed to Phase 2

4. **If mismatches DON'T exist:**
   - Identify which commit fixed them (git bisect between the versions)
   - Document the fix
   - Verify it doesn't mask real issues
   - Clear to proceed with Phase 1 implementation

### Phase 2: If Mismatches Exist — Detailed Analysis

1. **Create a recording diagnostic for the affected seed**
   - Modify `/tmp/freecell_flat_only_diagnostic.cpp` to:
     - Record board state at EVERY operation (not just flat-only hits)
     - At first `lru=MISS, flat=HIT` mismatch, search backward for the earlier operation with matching payload
     - Output detailed board states, descriptors, and hash values for comparison

2. **Identify the matching operation**
   - For op 221 (or the first mismatch), find which earlier op has the same:
     - Payload (bytes 3-31)
     - OR just matching hash from flat cache's perspective

3. **Compare board states**
   - Extract full board state at op 221
   - Extract full board state at the earlier matching op
   - Determine: Are these genuinely equivalent states?
   - If yes: Document as legitimate deduplication (pile-ordering artifact)
   - If no: This is a real bug in the flat cache

4. **If bug is confirmed:**
   - Report with specific board states and descriptor diffs
   - Decide: Fix before Phase 1, or document as known issue?

---

## Key Files to Check

- `src/test/unit_tests/mismatch_analyzer.cpp` — The test that found the issue (location: CMakeLists.txt line 140)
- `src/test/unit_tests/dual_cache_test.cpp` — FreeCell uses `run_flat_better_agreement_test()` here
- `src/main/game/search-state/game_state.cpp` — Lines 468-497 (waste_ptr handling), lines 1153-1220 (init_payload_and_hash)
- `src/main/game/flat_cache.h/cpp` — Flat cache implementation
- `src/main/game/global_cache.h/cpp` — LRU cache implementation

---

## Summary for Next Session

**What:** FreeCell seed 1 showed hundreds of `lru=MISS, flat=HIT` mismatches at op 221+
**Why it matters:** Could be a flat cache bug, or could be legitimate deduplication
**What's unknown:**
- Has this been fixed? (Current diagnostic shows zero hits, but previous showed many)
- Which test class (MismatchAnalyzer) produces the issue?
- Are the earlier hits at op 221 genuinely the same state?

**To continue:**
1. Locate and run MismatchAnalyzer test
2. If mismatches exist, run the recording diagnostic to capture board states
3. Compare op 221 with its earlier duplicate to determine if it's a bug or intended behavior
