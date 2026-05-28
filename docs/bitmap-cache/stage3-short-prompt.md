# Bitmap Cache Stage 3 — Copy This Prompt

Copy everything below the line into Claude Code on the Web.

---

## Setup

```bash
git fetch origin
git checkout feature/bitmap-cache && git pull origin feature/bitmap-cache
git checkout -b feature/bitmap-stage3
```

## Task

Read `docs/bitmap-cache/stage3-prompt.md` for the full specification. That document
contains the exact code to add, which files to modify, and PR creation instructions.

In summary: add a `solvitaire-bitmap` variant binary and Level 1 regression oracle,
so bitmap cache changes are covered by regression tests.

**Before writing code**, read these files in order:

1. `CLAUDE.md`
2. `docs/bitmap-cache/stage3-prompt.md` (the detailed spec — read ALL of it)
3. `src/main/main.cpp` lines 131-165
4. `src/main/evaluation/benchmark.cpp` lines 152-220
5. `src/main/evaluation/solvability_calc.cpp` lines 189-217
6. `CMakeLists.txt` lines 260-281 and 679-708
7. `src/main/game/cache_interface.h`

Then follow the implementation steps and PR instructions in the detailed spec.

**Key points:**
- Add `SOLVITAIRE_BITMAP_ONLY` `#elif` dispatch blocks in 3 files (4 functions)
- Add `solvitaire-bitmap` variant binary target in CMakeLists.txt
- Generate `tests/oracles/level1_bitmap.json` using `regression_runner.py --regenerate`
- Add `regression_level1_bitmap` CTest target
- All 3 test gates must pass + new bitmap regression must pass
- PR targets `feature/bitmap-cache` (not `dev`)
