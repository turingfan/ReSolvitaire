# Session Pickup Prompt — 2026-04-13 (Commit C)

Use this prompt verbatim (or close to it) to start the next session:

---

We are working on branch `feature/pile-first-undo` of ReSolvitaire-caching.

Read `docs/refactoring/PICKUP.md` and tell me in one sentence what we are doing this session before writing any code.

The session goal is **Commit C**: rewrite `undo_built_group_move` with the pile-first approach, following the same pattern established in Commit B.

Wait for my confirmation before writing any code.

---

## Briefing for the Successor

### State of the Branch

Commits A and B are complete. The working tree is clean. Last commit is `4642bb0` (KI-4 doc update). The implementation code is in `ef06f5f`.

Key additions from Commit B (already in the code):
- `bool initially_face_up[52]` in `game_state.h`
- `init_initially_face_up()` populates it after `turn_face_up()` in all three constructors
- `undo_regular_move` uses `initially_face_up[cid]` to distinguish STARTING(0) from STARTING_FACE_UP(1) when `piles[m.from][1].is_face_down()`
- `VALIDATE_INLINE_UNDO` guard on `make_regular_move`'s undo stack push
- `log_pile_recovery_mismatch()` debug helper (inside `#ifdef VALIDATE_INLINE_UNDO`, fires only on mismatch)

### What Commit C Needs to Do

Rewrite `undo_built_group_move` in the same style as `undo_regular_move`. The full plan pseudocode is in `/Users/ipg/.claude/plans/dazzling-gathering-thacker.md` under "Step 3: Rewrite `undo_built_group_move`".

### Critical Difference from Plan Pseudocode

The plan's Step 3 pseudocode uses the **old face-down heuristic** for the bottom card's descriptor:

```cpp
if (piles[m.from][m.count].is_face_down()) {
    old_desc = compact_state::STARTING_FACE_UP;  // ← WRONG
```

This has the **same bug** that was fixed in Commit B. The bottom card of a built group can be an initially-face-up card (e.g., the top card of an initial Klondike pile). In that case the correct descriptor is `STARTING=0`, not `STARTING_FACE_UP=1`.

The correct rule (same as `undo_regular_move`, adjusted for index):

```cpp
if (piles[m.from].size() > static_cast<pile::size_type>(m.count)
        && piles[m.from][m.count].is_face_down()) {
    old_desc = initially_face_up[bottom_cid]
        ? compact_state::STARTING
        : compact_state::STARTING_FACE_UP;
}
```

Apply the same original_tableau_piles range check as in `undo_regular_move`.

### Reveal Card Location

In `undo_built_group_move`, after the group is returned to `m.from`, the revealed card is at `piles[m.from][m.count]` — one below the bottom of the returned group. This is different from `undo_regular_move` where it is at `piles[m.from][1]`.

The reveal undo must happen **before** the descriptor recovery, because the face-down check at index `m.count` depends on the card being face-down again (reveal undo turns it back to face-down).

### Test Commands

```bash
# From cmake-build-debug/ (with VALIDATE_INLINE_UNDO=ON)
cmake -DVALIDATE_INLINE_UNDO=ON .. && make -j4

# Key tests
./bin/unit_tests --gtest_filter="ZobristIncremental.*:FaceUpCards.*"

# Full suite (5 pre-existing failures expected: Accordion.* x4, BlackHoleUsesNewCache)
./bin/unit_tests
```
