# Phase 1 Status: Pile-First Undo — Pickup Document

**Date written:** 2026-04-13
**Branch:** `feature/pile-first-undo`
**Last good commit:** `52b8668` — "feat: add recover_pre_move_descriptor helper (Phase 1 Step A)"
**Status:** Commit B implemented, tests passing — READY TO COMMIT

---

## PROCESS RULE — READ FIRST

**When a bug is discovered during implementation, STOP and report it to Ian immediately. Do NOT spend time investigating or attempting to fix it. Describe what failed, what the symptom is, and ask Ian how to proceed. Do not use up session context on silent investigation.**

---

## What We Are Trying to Do

Eliminate the `zobrist_undo_stack` from `game_state`. Instead of storing old hash/payload values on a stack during `make_*` moves and restoring them during `undo_*` moves, we recover all values from the restored pile state after the pile operations run.

The full plan is in `/Users/ipg/.claude/plans/dazzling-gathering-thacker.md`.

The plan has 6 commits: A (helper), B (undo_regular_move), C (undo_built_group_move), D (undo_stock_k_plus_move), E (undo_stock_to_all_tableau_move), F (final cleanup).

Commits A and B are done. Next: **Commit C** (`undo_built_group_move`).

---

## Current State of the Code

### Committed:
- `52b8668`: `recover_pre_move_descriptor` helper added (Commit A — superseded, now deleted)

### Uncommitted working tree changes (ready to commit as Commit B):
- `game_state.h`: added `bool initially_face_up[52]`, `init_initially_face_up()`, removed `recover_pre_move_descriptor`
- `game_state.cpp`:
  - `init_initially_face_up()`: records logical initial face-up state, fixes IN_SPACE for seed-constructor single-card piles, guarded for accordion/predecessor-cache games
  - `init_payload_and_hash()`: skip PARENT_x assignment when parent card is face-down (new rule: face-up card above face-down parent gets STARTING=0)
  - `make_regular_move`: `zobrist_undo_stack` push guarded with `#ifdef VALIDATE_INLINE_UNDO`
  - `undo_regular_move`: fully rewritten with pile-first approach, inline static lookup replaces `recover_pre_move_descriptor`
  - `log_pile_recovery_mismatch()`: debug helper (only outputs on mismatch, inside `#ifdef VALIDATE_INLINE_UNDO`)
  - `recover_pre_move_descriptor`: deleted

### Test status (with `-DVALIDATE_INLINE_UNDO=ON`):
- `ZobristIncremental.*` (13 tests): ALL PASS
- `FaceUpCards.*` (11 tests): ALL PASS
- `Accordion.*` (4 tests): FAIL — **pre-existing**, present on commit A before our changes
- `SolverCacheSelectionTest.BlackHoleUsesNewCache`: FAIL — **pre-existing**, present on commit A

---

## Semantic Changes Made in Commit B (needs doc update)

Three descriptor semantic fixes discovered and applied during this session:

1. **Face-up card above face-down parent → STARTING(0), not PARENT_x**
   Fixed in `init_payload_and_hash` positional loop. Face-down parent means no visible build relationship; correct descriptor is STARTING. Rationale: `determine_destination_descriptor` is never called with face-down parents (illegal move target), so only init-list/JSON constructors create this case.

2. **Initially-face-up single-card tableau pile → IN_SPACE(9), not STARTING(0)**
   Fixed in `init_initially_face_up()` fixup. In seed constructor, init_payload_and_hash runs before turn_face_up, so single-card pile top card is face-down at init time → STARTING=0. After turn_face_up it's face-up. Correct semantic is IN_SPACE (equivalent to "placed in an empty space"). Fixed by updating descriptor after turn_face_up.

3. **KI-2 naming anomaly (pre-existing, unresolved)**
   STARTING(0) is assigned to cards that START face-up in seed-constructor games (counterintuitive). STARTING_FACE_UP(1) is for cards revealed during play. Names are backwards. Renaming deferred.

---

## Pile-First Undo Logic for undo_regular_move

The pile-first code after pile undo in `undo_regular_move`:

```
old_desc = determine_destination_descriptor(m.from, moved)  // default
if m.from is in original tableau piles:
    if pile size >= 2 AND piles[m.from][1].is_face_down():
        // Two cases requiring static lookup (determine_destination_descriptor wrong):
        // - initially_face_up[cid]=true: initially face-up top card (seed) → STARTING=0
        // - initially_face_up[cid]=false: revealed card at original position → STARTING_FACE_UP=1
        old_desc = initially_face_up[cid] ? STARTING : STARTING_FACE_UP
    // else: face-up parent below OR single-card pile → determine_destination_descriptor correct
```

Note: `determine_destination_descriptor` returns IN_SPACE for single-card piles (pile size == 1), which is now correct after the init fixup. No special case needed.

---

## Known Issues / Pre-Merge Checklist

**KI-1: Static initial state lookup not valid for 2-deck games**
`initially_face_up[52]` indexed by CID. In 2-deck games, two physical copies of the same card share a CID. Practical impact unclear (2-deck games use `lru_cache`). Must be verified before Phase 1 is considered complete for 2-deck games.

**KI-2: Misleading descriptor names**
STARTING(0) for initially-face-up cards, STARTING_FACE_UP(1) for revealed cards. Backwards from expected. Deferred.

**KI-3: Pre-existing Accordion/BlackHole test failures**
`Accordion.*` (4 tests) and `BlackHoleUsesNewCache` fail with `VALIDATE_INLINE_UNDO=ON` in debug build. Confirmed present on commit A (before Commit B changes). Not caused by our work. Deferred.

**KI-4: `init_payload_and_hash()` runs before `turn_face_up()` in seed constructor**
This ordering means initially-face-up tableau cards are face-down at init time and get `STARTING=0` rather than positional descriptors. The Commit B `init_initially_face_up()` fixup patches up the single-card case (→ IN_SPACE), and the `initially_face_up[]` lookup handles undo correctly. But the cleaner fix would be to run `init_payload_and_hash()` after `turn_face_up()` in the seed constructor. Deferred to avoid risk of unintended side effects during the pile-first undo refactor.

---

## Build Commands for Testing

```bash
# From repo root
./build.sh --debug --unit-tests  # regular debug build

# From cmake-build-debug/
cmake -DVALIDATE_INLINE_UNDO=ON .. && make -j4

# Run ZobristIncremental + FaceUpCards (key tests for pile-first undo)
./bin/unit_tests --gtest_filter="ZobristIncremental.*:FaceUpCards.*"

# Run full unit tests (expect 5 pre-existing failures)
./bin/unit_tests
```
