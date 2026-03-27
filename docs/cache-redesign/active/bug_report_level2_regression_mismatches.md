# Bug Report: Level 2 Regression Mismatches — canfield-strict & fortunes-favor

## Summary

Two Level 2 regression failures at streamliner "none" are confirmed as real flat cache
bugs via mismatch diagnostic (2026-03-26). Both games use custom rules files in
`tests/rules/`. Neither is a preset.

**Status**: OPEN — investigation incomplete, root cause identified for fortunes-favor,
still under investigation for canfield-strict.

---

## Bug 1: fortunes-favor seed 31646033 — FALSE NEGATIVES (waste pointer)

**Symptom**: 18,908 LRU=HIT, flat=MISS mismatches. Node count increased 73%
(198,928 vs oracle 114,880). Outcome (unsolvable) is correct.

**Evidence** (from `/tmp/mismatch_diagnostic_fortunes-favor_seed31646033.txt`):
- First mismatch at op 22. Board identical to op 13.
- Descriptors: NO DIFFERENCES between ops 13 and 22.
- Payload byte 5 (waste_ptr): `0x12` (18) at op 13, `0x13` (19) at op 22.
- Hashes differ (consistent with differing payloads).

**Likely root cause**: `make_regular_move()` does NOT update the waste pointer when
a card is moved FROM the waste pile. Fortunes-favor uses `auto-waste-then-stock`
spaces policy, which auto-plays waste cards to empty tableau spaces via regular moves.
The waste pile shrinks but the payload's waste_ptr stays stale.

`make_regular_move()` sets `undo.old_waste_ptr = 255` (sentinel = "don't restore")
and never calls `update_waste_ptr_in_hash()`. Only `make_stock_move()` updates waste_ptr.

**Fix direction**: In `make_regular_move()`, detect when `m.from == waste` and call
`update_waste_ptr_in_hash(effective_waste_ptr())`. Save old value in undo for restore.

**Game rules**: `tests/rules/fortunes-favor.json` — 12 piles, same-suit build,
`auto-waste-then-stock`, stock size 36, foundations initial "all", NO redeal.

---

## Bug 2: canfield-strict seed 4000100 — FALSE POSITIVES

**Symptom**: 87 LRU=MISS, flat=HIT mismatches. Node count decreased (fewer nodes
with flat cache). Outcome (unsolvable) is correct but **false positives are dangerous** —
they cause the solver to skip genuinely new states.

**Evidence** (from `/tmp/mismatch_diagnostic_canfield-strict_seed4000100.txt`):
- First false positive at op 674. No earlier op has the same board.
- Ops 418 and 673 have MATCHING payload (bytes 3-31) but DIFFERENT board states.
- Same hash at ops 418, 673, 674.
- 87 total false positives, 3 evictions.

**Board difference between op 418 and op 674**:
- Op 418: Pile 3 = [5H, 4C, 3H, 2S, AH], Pile 4 = [AD, KS]
- Op 674: Pile 3 = [5H, 4C, 3H, 2S, AH, KS], Pile 4 = [AD]
- KS moved from pile 4 to bottom of pile 3. AD/AH swap between ROOT and IN_SPACE.
- Foundations, reserve, stock, waste are ALL identical between the two states.

**Investigation needed**: The descriptors for AD and AH should differ between these
states (one gets ROOT=2, the other IN_SPACE=9, and they swap). Since they are different
cards at different payload positions, the payloads SHOULD differ. But the diagnostic
reports identical payloads. Possible explanations to investigate:
1. Reserve encoding — canfield-strict has a stacked reserve of 13 cards. Is the reserve
   state fully captured in the payload? If reserve cards share descriptors with tableau
   cards, auto-reserve moves could cause conflation.
2. The parent_table has no wrapping — King(13) has no parents (line 17 of parent_table.cpp).
   AD on KS and AH on KS both get ROOT as fallback. But they're different card IDs, so
   the nibbles should still differ.
3. Could be a different board difference than the one visually identified — need to dump
   per-card descriptors at both ops to confirm.

**Game rules**: `tests/rules/canfield-strict.json` — 4 piles, red-black build,
whole-pile moves, `auto-reserve-then-waste`, foundations base "random", stock size 34,
deal count 3, redeal true, stacked reserve size 13.

---

## Diagnostic Infrastructure

Tests added to `src/test/unit_tests/mismatch_diagnostic.cpp`:
- `MismatchDiagnostic.FortunesFavorSeed31646033` — loads rules from file, 60s timeout
- `MismatchDiagnostic.CanfieldStrictSeed4000100` — same

New method `run_diagnostic_from_file()` supports custom rules JSON files (not just presets).
Also added IN_SPACE(9) to `desc_name()` helper.

Run from repo root (not build dir) so relative paths to `tests/rules/` resolve:
```bash
cmake-build-release/bin/unit_tests --gtest_filter="MismatchDiagnostic.FortunesFavorSeed31646033"
cmake-build-release/bin/unit_tests --gtest_filter="MismatchDiagnostic.CanfieldStrictSeed4000100"
```

---

## Relationship to Previous Bugs

These follow the same pattern as the STARTING and ROOT bugs: the flat cache payload
diverges from what the LRU cache computes for the same board state. The fortunes-favor
bug is a new variant — not a descriptor issue but a **metadata field** (waste_ptr) that
isn't maintained during certain move types. The canfield-strict bug may involve reserve
encoding or a subtle descriptor issue not yet identified.
