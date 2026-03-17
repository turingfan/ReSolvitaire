# Phase 1 Summary: Level 1 - Rapid Regression Suite
**Branch:** `testing-infrastructure`  
**Completion Date:** 2026-03-17

## Executive Summary

Phase 1 successfully established a robust, automated foundation for regression testing in ReSolvitaire. The core achievement is a "round-trippable" JSON-based pipeline that allows the solver's state to be exported, modified, and reloaded without loss of information.

### Key Deliverables:
1.  **Standardized JSON Output:** Added `--json` for machine-readable results and `--reveal-hidden` for transparent state export.
2.  **Diverse Regression Corpus:** Curated 150 unique JSON instances covering 15 game varieties (Klondike, Free Cell, Gaps, Black Hole, Golf, etc.).
3.  **Baseline Oracle:** Established a verified ground truth (`baseline_oracle.json`) containing search complexity metrics for the entire corpus. **Paths are standardized as relative to the `tests/` directory root.**
4.  **Automated CI Harness:** Integrated a Python-based regression runner into the build system (`ctest -R regression_level1`).

---

## Developer Details

### Technical Changes Made
- **JSON Core Refactor:** Replaced ad-hoc string building with **RapidJSON**, ensuring well-formed output and standardizing the schema.
- **Transparency Fixes:** Implemented a lowercase-suit convention (e.g., `ah` vs `AH`) to represent face-down cards in the JSON export, allowing Klondike and other "hidden card" games to be fully round-tripped.
- **Schema Hardening:** Developed a comprehensive JSON schema in `deal_parser.cpp` to validate incoming deals, ensuring strict compliance and early error detection.
- **CTest Integration:** Added a new test target that executes a custom Python runner, comparing live solver output against the baseline oracle for `solution_type`, `states_searched`, and `backtracks`.

### Obstacles & Hurdles Overcome

#### 1. The "Hidden Card" Information Leak
**Problem:** In the original export, face-down cards were masked as `##`. This made it impossible to reload a Klondike game state from JSON because the solver didn't know which card was underneath.
**Solution:** Updated the `card` class and JSON export logic to reveal the card identity while stripping the "visible" flag. By using lowercase suits (the established Solvitaire input convention), we restored full state transparency without breaking the parser's logic.

#### 2. Sparse Pile Schema Conflicts
**Problem:** Games with sparse piles (like Cells in Free Cell or Reserves in Baker's Dozen) exported empty array slots as `""`. The rigid JSON schema validator rejected these as invalid "cards".
**Solution:** Revised the schema to allow `oneOf` (card or empty string) in array definitions. This maintains 1:1 mapping between the JSON array indices and the internal pile representation.

#### 3. Gaps Ambiguity (`AS` vs Ace of Spades)
**Problem:** In Gaps, `AS` is used as a placeholder for an empty space (a "gap"). However, `AS` also matches the regex for "Ace of Spades".
**Solution:** Adjusted the schema logic for sequences to use `anyOf` with a prioritized regex, ensuring that `AS` is correctly interpreted in the context of Gaps while still allowing normal card parsing.

#### 4. C++ Parser Robustness
**Problem:** The `deal_parser.cpp` was attempting to instantiate `card("")` for empty slots, leading to runtime crashes in `stoi`.
**Solution:** Implemented guards in the parser to skip empty strings during initialization, properly resulting in empty `pile` objects.

---

## Verification Status
- **Corpus Coverage:** 150/150 instances verified.
- **Round-Trip Status:** All instances can be exported via `--deal-only` and reloaded via `--json` with identical results.
- **Regression Pass:** `100% tests passed` in current `testing-infrastructure` branch.
