# Session Notes: Descriptor Bug Fixes (2026-03-26)

## Overview

This session fixed two interconnected bugs in the flat cache descriptor model
that caused both false negatives and false positives. The session ended with all
9 DualCacheTest agreement tests passing — zero mismatches in either direction.

## Bug 1: STARTING Descriptor Not Position-Canonical (False Negatives)

**Problem**: `init_payload_and_hash()` assigned STARTING(0) to all cards, but
`determine_destination_descriptor()` computed positional descriptors (ROOT,
PARENT_x). A card moved away and returned to its original position got a
different descriptor than it started with, causing flat cache MISS where LRU
correctly saw HIT.

**Fix**: Init now computes positional descriptors for all face-up tableau cards:
- Bottom of pile → IN_SPACE(9)
- Card on legal parent → PARENT_0-3
- Card on non-legal parent → ROOT(2)
- Face-down cards → STARTING(0)

This matches what `determine_destination_descriptor()` returns, so a card
returning to any position gets the same descriptor it would have had at init.

**Affected games**: FreeCell, BakersGame, Somerset (all had LRU=HIT, flat=MISS).

## Bug 2: ROOT Descriptor Overloaded (False Positives)

**Problem**: ROOT(2) was used for three different situations:
1. Card at bottom of pile in starting position
2. Card moved to an empty pile
3. Card sitting on a non-legal-build parent (fallback)

In SpanishPatience (ANY_SUIT build, 13 piles), cards are dealt in random
non-legal-build order. A card like 4S sitting on 9D (not a legal parent) got
ROOT as a fallback. When 9D was moved away, 4S became the bottom card and also
got ROOT. The payloads were identical for genuinely different board states.

**Fix**: Introduced IN_SPACE(9) and redefined ROOT(2):
- **IN_SPACE(9)** = card at bottom of pile (nothing below, or only face-down)
- **ROOT(2)** = card on a non-legal-build parent (fallback from parent table)

Now 4S on non-legal 9D gets ROOT(2), while 4S at the bottom of a pile gets
IN_SPACE(9) — different descriptors, different payload, no false positive.

**Affected games**: SpanishPatience, FlowerGarden, SeahavenTowers.

## Bug 3: Waste Pointer Symmetry (Preventive Fix)

**Problem**: LRU cache applies `waste_deal_symmetry` when `stock_redeal &&
waste_size % deal_count == 0`, treating the waste pointer as irrelevant in
those states. Flat cache did not apply the same condition.

**Fix**: Added `effective_waste_ptr()` that returns 0 when waste_deal_symmetry
holds, used in both init and stock moves.

## Reveal Handling for Hidden-Card Games

When a face-down card is revealed (e.g., Klondike), the descriptor depends on
position:
- Bottom of pile (pile size == 1 after reveal) → IN_SPACE(9)
- Not bottom (face-down cards still below) → STARTING_FACE_UP(1)

## Updated Descriptor Semantics

| Value | Name | Meaning |
|---|---|---|
| 0 | STARTING | Face-down card in original position; also foundation cards |
| 1 | STARTING_FACE_UP | Originally face-down, now revealed, NOT bottom of pile |
| 2 | ROOT | On a non-legal-build parent (parent_table fallback) |
| 3 | IN_CELL | In a free cell |
| 4-7 | PARENT_0-3 | On a legal build parent (by fixed suit ordering) |
| 8 | IN_HOLE | Played to hole (hole games only) |
| 9 | IN_SPACE | Bottom of tableau pile (empty space below) |
| 10-15 | RESERVED | Unused |

## Key Principle

Init descriptors must match move descriptors for the same position. This was
the root cause of both bugs: init used one value (STARTING for bug 1, ROOT for
bug 2), but moves computed a different value for the same position. The fix in
both cases was the same: make init compute exactly what moves would compute.

## Debugging Approach

1. `mismatch_diagnostic.cpp` — records every op's payload, hash, and board state
   during DFS with a dual cache. On first mismatch, searches backward for
   matching payloads and boards to determine if it's a false positive or false
   negative.

2. For false negatives (LRU=HIT, flat=MISS): looked for earlier op with same
   board but different payload → found STARTING vs PARENT discrepancy.

3. For false positives (flat=HIT, LRU=MISS): looked for earlier op with same
   payload but different board → found ROOT descriptor conflation.

## Files Changed

- `src/main/game/compact_state.h` — added IN_SPACE(9) to descriptor enum
- `src/main/game/search-state/game_state.cpp` — all descriptor assignment logic:
  - `init_payload_and_hash()`: positional descriptors at init
  - `determine_destination_descriptor()`: IN_SPACE for empty pile, ROOT for fallback
  - `make_regular_move()`: reveal → IN_SPACE if bottom, STARTING_FACE_UP otherwise
  - `make_built_group_move()`: same for built group moves and reveals
  - `effective_waste_ptr()`: new method for waste symmetry
- `src/main/game/search-state/game_state.h` — `effective_waste_ptr()` declaration
- `src/test/unit_tests/mismatch_diagnostic.cpp` — diagnostic test infrastructure
- `CMakeLists.txt` — added mismatch_diagnostic.cpp to test sources
