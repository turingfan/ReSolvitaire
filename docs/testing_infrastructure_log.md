# Development Log - `testing-infrastructure` Branch

**Date:** 2026-03-17  
**Status:** AI-Generated Documentation

This document logs the major changes implemented on the `testing-infrastructure` branch of the ReSolvitaire project. This branch is dedicated to building a robust, zero-tolerance regression testing framework and CI harness for the solver.

---

## 1. Branch Initialization
**Date:** 2026-03-17  
**Rationale:** Established a dedicated workspace for implementing Phase 1 (Level 1) of the AI Agent Implementation Roadmap, focusing on a rapid regression suite and machine-readable output.

---

## 3. Infrastructure: Technical Fixes and RapidJSON Refactor
**Commit Stage 2:** `8ef9d1f`  
**Date:** 2026-03-17 10:15:00 (approx)
**Reason:** Resolved critical issues where hidden cards in Klondike were masked as `##` (losing identity) and JSON foundations/cells/reserve were incorrectly exported as 2D arrays (violating the input schema).
**Changes:**
- **Full Transparency:** Implemented `--reveal-hidden` flag to export face-down cards using lowercase-suit convention (e.g., `10h`), enabling lossless JSON deal reconstruction.
- **RapidJSON Refactor:** Replaced `Boost.PropertyTree` with direct `RapidJSON::Writer` calls in `json_helper.cpp`. This fixed empty array rendering (e.g., `[]` instead of `""`) and ensured 1D array dimensionality for foundations, cells, and reserve.
- **Corpus Regeneration:** Regenerated the 110-instance corpus to include full card identity for all face-down cards.
- **Verification:** Successfully performed a "round-trip" solve for a Klondike deal (loading a JSON exported with `--reveal-hidden`).

---

## 4. Corpus Expansion: Diverse Game Varieties
**Commit Stage 3:** `75510d0`  
**Date:** 2026-03-17 10:22:00 (approx)
**Rationale:** Expanded the Level 1 regression suite to 150 instances (+40 from previous) to cover a broader range of rule behaviors and solver logic.
**Games Added:**
- **Gaps (Basic Variant):** Tests hole/gap movement logic.
- **Black Hole:** Tests one-pile sequence building.
- **Golf:** Tests waste building with no foundation movement.
- **Late-Binding Solitaire:** Tests complex conditional rule logic.

---

## 5. Finalizing JSON Pipeline & Baseline (Step 1.3)
**Commit Stage 4:** `8b51773`  
**Date:** 2026-03-17 10:45:00 (approx)
**Rationale:** Resolved schema validation conflicts for sparse arrays (Cells/Reserves) and gaps. Successfully established the regression ground truth.
**Changes:**
- **Schema Hardening:** Modified `deal_schema_json` in `deal_parser.cpp` to use `anyOf` and `enum: [""]`, allowing empty strings in cell, reserve, and accordion arrays. This permits a 1:1 mapping between JSON array indices and game state piles.
- **Parser Robustness:** Updated `deal_parser.cpp` to skip `place_card` calls for empty strings, preventing `stoi` conversion errors.
- **Baseline Secured:** Generated `tests/level1/baseline_oracle.json` containing the search metrics for all 150 instances.
- **Verification:** Verified that all 150 instances can be reloaded and solved, producing results identical to the oracle.

---

## 6. CI Harness: Automated Regression Tests (Step 1.4)
**Commit Stage 5:** `2e18b75`  
**Date:** 2026-03-17 11:15:00 (approx)
**Rationale:** Automated common tasks to ensure search stability and correctness across all 15 game types.
**Changes:**
- **Runner Script:** Created `src/test/regression_runner.py`. This script iterates through the `instances/` directory, runs the solver against each deal, and compares `solution_type`, `states_searched`, and `backtracks` with the values in `baseline_oracle.json`.
- **CTest Integration:** Added `regression_level1` to `CMakeLists.txt`.
- **Verification:** Ran `ctest -R regression_level1`. Result: **150/150 instances passed**.

---

### Step 1.5: Project Hierarchy Cleanup
- **Objective**: Consolidate test data and separate orchestration scripts from source code.
- **Changes**:
    - Created root-level `scripts/` directory.
    - Moved `regression_runner.py`, `generate_baseline.py`, and `check_output_format.py` (refactored to `argparse`) to `scripts/`.
    - Consolidated `src/test/resources/` into `tests/resources/`.
    - Updated `CMakeLists.txt` to reflect new paths.
    - Verified all tests pass via `ctest`.
- **Git Commit**: `8b45017`
