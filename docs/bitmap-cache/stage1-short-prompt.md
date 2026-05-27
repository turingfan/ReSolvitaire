# Bitmap Cache Stage 1 — Copy This Prompt

Copy everything below the line into Claude Code on the Web.

---

## Setup

```bash
git fetch origin
git checkout feature/bitmap-cache && git pull origin feature/bitmap-cache
git checkout -b feature/bitmap-stage1
```

## Task

Read `docs/bitmap-cache/stage1-prompt.md` for the full specification. That document
contains the class design, implementation details, test list, adapted rules for
autonomous operation, and PR creation instructions.

In summary: implement a standalone `bitmap_cache` class (1-bit-per-entry transposition
table) with unit tests. No solver integration yet.

**Before writing code**, read these files in order:

1. `CLAUDE.md`
2. `docs/bitmap-cache/stage1-prompt.md` (the detailed spec — read ALL of it)
3. `docs/bitmap-cache/domain-questions.md`
4. `src/main/game/platform_memory.h`
5. `src/main/game/cache_interface.h`
6. `src/main/game/generic_flat_cache.h` lines 1-50
7. `src/test/unit_tests/generic_flat_cache_test.cpp` lines 1-60

Then follow the implementation steps and PR instructions in the detailed spec.

**Key points:**
- `bitmap_cache` inherits `cache_interface`, uses `platform::lazy_buffer` for mmap
- Power-of-2 bit count, index = `hash & mask`, one bit per state
- `probe_and_insert(uint64_t hash)` is the core operation
- Unit tests go in `src/test/unit_tests/bitmap_cache_test.cpp`
- `SearchTraceAgreementTest.HashOnlyVsFlat_Klondike50Seeds` is a known pre-existing
  failure — see the detailed spec for workaround commands
- PR targets `feature/bitmap-cache` (not `dev`)
