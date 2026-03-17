# Development Log - `mac-dev` Branch

**Date:** 2026-03-13  
**Status:** AI-Generated Documentation

This document logs the major changes implemented on the `mac-dev` branch of the ReSolvitaire project. The primary goal of this branch is to establish a robust, native development environment for macOS, removing platform-specific constraints and legacy infrastructure.

---

## 1. Branch Initialization
**Date:** 2026-03-13  
**Rationale:** Established a dedicated workspace for platform-specific and modernization efforts without impacting the stable branch.

---

## 2. Infrastructure: Removal of Docker Environment
**Commit Stage 1:** `e38dc99`  
**Date:** 2026-03-13 15:09:54 +0000  
**Reason:** The Docker environment, while useful for containment, was identified as a distraction for native macOS development. Removing it reduces repository bloat and simplifies the development lifecycle.  
**Changes:**
- Deleted `Dockerfile`, `docker-install.sh`, and `enter-container.sh`.
- Removed Docker service dependency from `.travis.yml`.
- Cleaned up `README.md` to remove Docker-centric installation instructions.
- Deleted `docs/windows_cheat_sheet.md` due to its heavy reliance on Docker.

---

## 3. Build System: CMake Modernization & Portability
**Commit Stage 2:** `82c097d`  
**Date:** 2026-03-13 15:10:01 +0000  
**Reason:** Improvements to the build system were necessary to support native compilation on macOS and to simplify dependency management.  
**Changes:**
- **GoogleTest Integration:** Switched from a manual download shell script to CMake's modern `FetchContent` module, ensuring reliable dependency retrieval across platforms.
- **IDE Support:** Implemented `source_group` categorization in `CMakeLists.txt` to logically organize files into groups (Game, Solver, Evaluation, IO, Test) when viewed in IDEs like CLion or VS Code.
- **Script Portability:** Updated `build.sh` to fix bash-specific syntax incompatible with default macOS bash and removed strict argument order dependencies.
- **Cleanup:** Removed approximately 200 lines of dead or commented-out targets from `CMakeLists.txt`.

---

## 4. Code: Modernization and Portability Fixes
**Commit Stage 3:** `94f868a`  
**Date:** 2026-03-13 15:10:09 +0000  
**Reason:** Addressed compiler warnings and removed Linux-specific headers that caused build failures on macOS.  
**Changes:**
- Removed `#include <malloc.h>` in `solver.cpp` (not standard/necessary on macOS).
- Fixed `friend` declarations for `structs` incorrectly labeled as `classes`.
- Added missing `<set>` header in `game_state.h`.
- Refined namespace usage and fixed minor C++14/17 compatibility issues across core files.

---

## 5. Documentation: Development Log Creation
**Commit Stage 4:** `24ffd17`  
**Date:** 2026-03-13 15:10:26 +0000  
**Reason:** Documenting the branch's evolution and providing a clear path for future platform unification.

---

## 6. Repository Cleanup: Finishing Deletions
**Commit Stage 5:** `9c7b819`  
**Date:** 2026-03-16 18:06:48 +0000  
**Reason:** Completed the removal of legacy Docker and Windows-centric files that were missed in Stage 1 but identified as part of the repository modernization.  
**Changes:**
- Deleted `CMakeLists.txt.in`, `Dockerfile`, `docker-install.sh`, `docs/windows_cheat_sheet.md`, and `enter-container.sh`.
- Updated `.gitignore` to include more robust build directory exclusions.

---

## 7. Documentation: Solvitaire Results Dataset Overview
**Commit Stage 6:** `939a48b`  
**Date:** 2026-03-17 09:33:00 +0000  
**Reason:** Documenting the location and structure of the large Solvitaire results dataset used in the original paper.
**Changes:**
- Created `docs/solvitaire_results_overview.md` with dataset mapping and verification commands.

---

---

## 8. Regression Suite: Ground Truth & Multi-Run Infrastructure
**Date:** 2026-03-17  
**Reason:** Documenting critical, non-obvious ground truth regarding the original experimental logs and the requirements for deterministic regression verification.
**Key Insights:**
- **Smart Streamliner (Multi-Run):** In datasets using the "smart" streamliner, the first run (using `both` streamliners) cannot be trusted for unsolvability. A solution found in Run 1 is valid, but an unsolvable result requires a second run (using `none` streamliner) for confirmation.
- **Timeout Regression Criteria:** To account for hardware variance, a timeout on a regression test is considered a failure ONLY if the node count exceeds the baseline. If the node count is lower or equal, it is categorized as a speed regression (acceptable) rather than a logic failure.
- **Mapping Metadata:** The files `AAA-smartfiles` and `AAA-singlerunfiles` in `AnalysisScripts` provide the definitive mapping of experimental datasets to their respective streamliner configurations.
- **Custom Game Rules:** Integration of `--custom-rules` support for game types not included in the solver's internal preset list, using rule definitions from the `GameJSON` directory.
