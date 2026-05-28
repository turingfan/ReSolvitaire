# Bitmap Cache Stage 2 — Copy This Prompt

Copy everything below the line into Claude Code on the Web.

---

## Setup

```bash
git fetch origin
git checkout feature/bitmap-cache && git pull origin feature/bitmap-cache
git checkout -b feature/bitmap-stage2
```

## Task

Read `docs/bitmap-cache/stage2-prompt.md` for the full specification. That document
contains the exact code to add, which files to modify, and PR creation instructions.

In summary: integrate `bitmap_cache` into the solver via a new `BitmapPolicy` so that
`--cache-type bitmap` works for all single-deck non-accordion games.

**Before writing code**, read these files in order:

1. `CLAUDE.md`
2. `docs/bitmap-cache/stage2-prompt.md` (the detailed spec — read ALL of it)
3. `src/main/game/bitmap_cache.h`
4. `src/main/game/cache_policy.h`
5. `src/main/game/cache_interface.h`
6. `src/main/main.cpp` lines 118-160
7. `src/main/input-output/input/command_line_helper.cpp` lines 91 and 261-263

Then follow the implementation steps and PR instructions in the detailed spec.

**Key points:**
- Add `BitmapPolicy` to `cache_policy.h` (copies FlatPolicy pattern, uses `bitmap_cache`)
- Add `use_bitmap_cache()` to `cache_interface.h`
- Add `"bitmap"` to valid cache types in `command_line_helper.cpp`
- Add dispatch in `main.cpp`, `benchmark.cpp`, `solvability_calc.cpp`
- Smoke test with `--cache-type bitmap` on klondike, free-cell, black-hole
- All 3 test gates must pass
- PR targets `feature/bitmap-cache` (not `dev`)
