# Session Pickup Prompt — 2026-04-13 (Commit D)

Use this prompt verbatim (or close to it) to start the next session:

---

We are working on branch `feature/pile-first-undo` of ReSolvitaire-caching.

Read `docs/refactoring/PICKUP.md` and tell me in one sentence what we are doing this session before writing any code.

The session goal is **Commit D**: rewrite `undo_stock_k_plus_move` with the pile-first approach, following the same pattern established in Commits B and C.

Wait for my confirmation before writing any code.

---

## Briefing for the Successor

### State of the Branch

Commits A, B, and C are complete. The working tree is clean. Last commit is `c98d1e5` (Commit C).

Key additions already in the code:
- `bool initially_face_up[52]` in `game_state.h`
- `init_initially_face_up()` populates it after `turn_face_up()` in all three constructors
- `undo_regular_move` and `undo_built_group_move` fully rewritten with pile-first approach
- `VALIDATE_INLINE_UNDO` guards on `make_regular_move` and `make_built_group_move` undo stack pushes
- `log_pile_recovery_mismatch()` debug helper (inside `#ifdef VALIDATE_INLINE_UNDO`, fires only on mismatch)
- Accordion (4) and Predecessor (11) tests disabled with `DISABLED_` prefix

### What Commit D Needs to Do

Rewrite `undo_stock_k_plus_move` in the same style as `undo_regular_move`. The full plan pseudocode is in `/Users/ipg/.claude/plans/dazzling-gathering-thacker.md` under "Step 4: Rewrite `undo_stock_k_plus_move`".

### Key Notes on undo_stock_k_plus_move

This move handles stock/waste dealing (Klondike draw-1, draw-3, etc.). Key differences from regular and built-group moves:

- The played card's old descriptor is **always `STARTING`** — stock/waste cards never have any other descriptor. No `initially_face_up` lookup needed.
- **Card identity must be read BEFORE pile undo** (card is at `piles[m.to]` while it is still there).
- **Waste pointer is always updated** (unconditionally, not conditional on `m.from == waste` like `undo_regular_move`). Call `effective_waste_ptr()` after all pile ops complete.
- Foundation and hole updates follow the same pattern as `undo_regular_move` (check `is_foundation_pile(m.to)` and `m.to == hole`).
- The `m.flip_waste` flag controls whether the waste was flipped back to stock during the make move; undo must reverse this.
- The `m.count` field controls stock/waste card transfers (positive = stock→waste, negative = waste→stock).

### VALIDATE_INLINE_UNDO Block

The reference path for the VALIDATE_INLINE_UNDO block in `undo_stock_k_plus_move` uses `undo_ref` fields:
- `undo_ref.to_found_suit` / `undo_ref.old_to_found_rank` — foundation (if destination was foundation)
- `undo_ref.old_hole_top` — hole top (if destination was hole)
- `undo_ref.old_waste_ptr` — old waste pointer
- `undo_ref.card_id` / `undo_ref.old_desc` — played card's descriptor (always STARTING)

### Test Commands

```bash
# From cmake-build-debug/ (with VALIDATE_INLINE_UNDO=ON)
cmake -DVALIDATE_INLINE_UNDO=ON .. && make -j4

# Key tests
./bin/unit_tests --gtest_filter="ZobristIncremental.*:FaceUpCards.*"

# Full suite (1 pre-existing failure expected: BlackHoleUsesNewCache)
./bin/unit_tests
```
