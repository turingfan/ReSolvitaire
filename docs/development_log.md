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

## Summary of Current State
The project now builds and passes all 133 unit tests natively on macOS using standard CMake tools. The repository is cleaner, better organized for IDE-based development, and free of obsolete deployment scripts.
