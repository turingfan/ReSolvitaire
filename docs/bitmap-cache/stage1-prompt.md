# Prompt for Claude Code on the Web — Bitmap Cache Stage 1

**Copy everything below the line into Claude Code on the Web.**

---

## Task

Implement the `bitmap_cache` data structure — a 1-bit-per-entry transposition table.
This is a standalone class with unit tests. **No solver integration in this stage.**
The bitmap cache does NOT use `generic_flat_cache` or any of its cluster/policy
machinery. It is its own class with its own header file.

## Setup

```bash
git fetch origin
git checkout feature/bitmap-cache
git pull origin feature/bitmap-cache
git checkout -b feature/bitmap-stage1
```

## Before You Write Code

Read these files in order. Do not start coding until you have read all of them.

1. `CLAUDE.md` — build commands, test gates, project rules
2. `docs/bitmap-cache/domain-questions.md` — where to log domain issues
3. `src/main/game/platform_memory.h` — the `platform::lazy_buffer` RAII class you must use for memory allocation (mmap lazy allocation, zero-initialized on first access)
4. `src/main/game/cache_interface.h` — the `cache_interface` base class your cache must inherit from, and the `game_state` type used in its virtual methods
5. `src/main/game/generic_flat_cache.h` lines 1-50 — for context on how the existing cache classes are structured (DO NOT reuse this machinery)
6. `src/test/unit_tests/generic_flat_cache_test.cpp` lines 1-60 — for the testing pattern (GTest, zobrist_hash::init(), game_state construction)

## Adapted Rules (for autonomous operation)

**Bug in EXISTING code:** Do NOT stop. Log the symptom clearly in the
"Domain Questions Log" section at the bottom of
`docs/bitmap-cache/domain-questions.md`. Continue with your best-effort
implementation. Mark the affected code with `// DOMAIN_QUESTION:` comment.

**Semantic/domain question you can't resolve from code or docs:** Log it in
the same place. Make your best guess and mark it with `// DOMAIN_QUESTION:`.

**Bug in YOUR new code (compilation errors, test failures):** Fix normally —
that's development. You may iterate up to 3 attempts. If still failing after
3 attempts, log the issue and submit what you have.

**Test gates:** All 3 test gates MUST pass before creating the PR. Run:
`python3 scripts/run_tests.py`. If a gate fails and you cannot fix it in 3
attempts, create the PR anyway, note the failure in the PR description, and
explain what you tried.

**Scope:** Do NOT modify files outside the scope listed in this prompt.
Do NOT refactor existing code. Do NOT add features beyond what is specified.

**One commit per logical step** (e.g. "data structure", "unit tests"). Not
one giant commit and not one per line change.

## Implementation Steps

### Step 1: Create `src/main/game/bitmap_cache.h`

Create a new header file with the following class:

```cpp
#ifndef SOLVITAIRE_BITMAP_CACHE_H
#define SOLVITAIRE_BITMAP_CACHE_H

#include "cache_interface.h"
#include "platform_memory.h"
#include <cstdint>

class bitmap_cache : public cache_interface {
    platform::lazy_buffer buffer;
    uint64_t num_bits;   // power of 2
    uint64_t mask;       // num_bits - 1

    // Statistics
    uint64_t total_probes = 0;
    uint64_t hit_count    = 0;
    uint64_t insert_count = 0;

public:
    explicit bitmap_cache(uint64_t capacity_bytes);

    // Core operation: test-and-set. Returns true if bit was ALREADY set (HIT).
    bool probe_and_insert(uint64_t hash);

    // Read-only probe. Returns true if bit is set.
    bool probe(uint64_t hash) const;

    // --- cache_interface overrides ---
    bool insert(const game_state& gs) override;
    bool contains(const game_state& gs) const override;
    void clear() override;
    uint64_t size() const override;
    uint64_t get_states_removed_from_cache() const override { return 0; }
    uint64_t bucket_count() const override;

    // --- Accessors ---
    uint64_t get_num_bits()     const { return num_bits; }
    uint64_t get_total_probes() const { return total_probes; }
    uint64_t get_hit_count()    const { return hit_count; }
    uint64_t get_insert_count() const { return insert_count; }
};

#endif // SOLVITAIRE_BITMAP_CACHE_H
```

**Implementation details for the constructor:**

```cpp
bitmap_cache::bitmap_cache(uint64_t capacity_bytes)
    : buffer(capacity_bytes),
      num_bits(/* see below */),
      mask(/* see below */)
{
    // Compute num_bits: capacity_bytes * 8, rounded DOWN to power of 2.
    // Use bit manipulation: find highest set bit.
    // If capacity_bytes == 0, set num_bits = 0, mask = 0.
    uint64_t total_bits = capacity_bytes * 8;
    if (total_bits == 0) {
        num_bits = 0;
        mask = 0;
        return;  // Note: lazy_buffer(0) must be handled — check if it works
    }
    // Round down to power of 2:
    //   num_bits = 1ULL << floor(log2(total_bits))
    // Use: num_bits = 1ULL << (63 - __builtin_clzll(total_bits))
    // But __builtin_clzll is undefined for 0, which we already excluded above.
    // ALTERNATIVELY, use a portable loop or bit trick. Pick whichever you prefer
    // but it MUST be correct for all uint64_t values.
}
```

**Implementation details for probe_and_insert:**

```cpp
bool bitmap_cache::probe_and_insert(uint64_t hash) {
    if (num_bits == 0) return false;  // degenerate: no cache
    uint64_t index = hash & mask;
    uint64_t byte_idx = index >> 3;
    uint8_t  bit_mask = uint8_t(1) << (index & 7);

    uint8_t* bytes = buffer.as<uint8_t>();
    bool was_set = (bytes[byte_idx] & bit_mask) != 0;

    ++total_probes;
    if (was_set) {
        ++hit_count;
    } else {
        bytes[byte_idx] |= bit_mask;
        ++insert_count;
    }
    return was_set;
}
```

**Implementation details for probe (read-only):**

Same as probe_and_insert but without the set operation or insert_count increment.
Still increments total_probes and hit_count (if hit).

**cache_interface overrides:**

- `insert(const game_state& gs)`: Call `gs.get_zobrist_hash()` to get the hash.
  Call `probe_and_insert(hash)`. Return `!result` (because cache_interface
  convention is true = newly inserted, but probe_and_insert returns true = was
  already present).
- `contains(const game_state& gs)`: Call `gs.get_zobrist_hash()`, then
  `probe(hash)`.
- `clear()`: Call `buffer.reset()` and reset all counters to 0.
- `size()`: Return `insert_count` (number of unique insertions — we can't count
  actual set bits efficiently, and insert_count is the correct logical size).
- `bucket_count()`: Return `num_bits`.

**Where to put the implementation:** You can either put everything in the header
(inline, like the simpler methods) or split into `.h` and `.cpp`. If you use a
`.cpp`, it must be added to the `sources` list in `CMakeLists.txt`. A header-only
approach avoids the CMakeLists change for source files but either approach is fine.

After completing this step, build to check compilation:
```bash
./build.sh --release --unit-tests
```

### Step 2: Create `src/test/unit_tests/bitmap_cache_test.cpp`

Write GTest unit tests. Follow the pattern in `generic_flat_cache_test.cpp`.

**Test fixture:**
```cpp
class BitmapCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }
};
```

**Required tests (minimum — add more if you see gaps):**

1. **Construction_PowerOf2Rounding** — Verify that `bitmap_cache(1000)` has
   `get_num_bits()` equal to the largest power-of-2 ≤ 8000 (which is 8192,
   wait — 1000 bytes = 8000 bits, largest power of 2 ≤ 8000 is 4096).
   Also test: `bitmap_cache(1024)` → 8192 bits. `bitmap_cache(128)` → 1024 bits.
   `bitmap_cache(256)` → 2048 bits.

2. **Construction_ZeroCapacity** — `bitmap_cache(0)` doesn't crash.
   `get_num_bits() == 0`.

3. **ProbeAndInsert_NewHash_ReturnsFalse** — Insert a hash, get false (miss).
   Insert the same hash again, get true (hit).

4. **ProbeAndInsert_DifferentHashes** — Insert two hashes that map to different
   bits (for a small cache, pick hashes carefully). Both should return false.

5. **Probe_ReadOnly** — Insert via probe_and_insert, then call probe() on same
   hash → true. Call probe() on never-inserted hash → false.

6. **Statistics** — After several operations, verify `get_total_probes()`,
   `get_hit_count()`, `get_insert_count()` are correct.

7. **Clear_ResetsState** — Insert some hashes, call clear(), verify they're gone
   and counters are reset.

8. **CacheInterface_Insert** — Using a `game_state` (construct one like in
   `generic_flat_cache_test.cpp`: `game_state gs(rules, 1, game_state::streamliner_options::NONE);`
   with `sol_rules rules = rules_parser::from_preset("free-cell");`). Insert it,
   verify returns true (new). Insert same state again, verify returns false (hit).

9. **CacheInterface_Contains** — Similar: insert a state, verify `contains()` is
   true. Make a move, verify `contains()` of the new state is false.

**Add the test file to CMakeLists.txt:**

Find the `set(sources_test_unit ...)` block (around line 124) and add:
```
        src/test/unit_tests/bitmap_cache_test.cpp
```

After completing this step, build and run tests:
```bash
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R ^unit_tests$ --output-on-failure
```

### Step 3: Run all 3 test gates

```bash
python3 scripts/run_tests.py
```

All 3 gates must pass. If any fail, fix and re-run (up to 3 attempts).

## Create PR

After all tests pass:

```bash
git add src/main/game/bitmap_cache.h src/test/unit_tests/bitmap_cache_test.cpp CMakeLists.txt
# Also add bitmap_cache.cpp if you created one
git status  # verify only expected files are staged

git commit -m "feat: add bitmap_cache data structure with unit tests

Implement 1-bit-per-entry transposition table (bitmap_cache class):
- Power-of-2 sized bit array using platform::lazy_buffer (mmap)
- probe_and_insert() for combined test-and-set operation
- probe() for read-only containment check
- cache_interface overrides for polymorphic access
- Hit/miss/probe statistics counters
- Unit tests covering construction, insertion, collisions, statistics, clear"

git push -u origin feature/bitmap-stage1
```

Then create the PR:

```bash
gh pr create \
  --base feature/bitmap-cache \
  --title "Stage 1: bitmap_cache data structure" \
  --body "$(cat <<'EOF'
## Summary

Stage 1 of the bitmap cache implementation (see `docs/bitmap-cache/stage1-prompt.md`).

- New `bitmap_cache` class: 1-bit-per-entry transposition table
- Uses `platform::lazy_buffer` for mmap lazy allocation
- Power-of-2 sizing with single-AND index computation
- No eviction, no payload, no clusters
- Inherits `cache_interface` for polymorphic access
- Unit tests in `bitmap_cache_test.cpp`

## Test Results

- [ ] Gate 1 (Release): pass/fail
- [ ] Gate 2 (Trace): pass/fail
- [ ] Gate 3 (Debug): pass/fail

## Domain Questions

(List any entries added to `docs/bitmap-cache/domain-questions.md`, or "None")

## Scope

**Files created:**
- `src/main/game/bitmap_cache.h` (and `.cpp` if split)
- `src/test/unit_tests/bitmap_cache_test.cpp`

**Files modified:**
- `CMakeLists.txt` (added test source)

No other files touched.
EOF
)"
```

## If You Get Stuck

1. **Compilation error you can't fix in 3 tries:** Comment out the failing code,
   add a `// STUCK:` comment explaining the issue, and proceed with the rest.
2. **Test failure you can't fix in 3 tries:** Log it in the PR description. Create
   the PR anyway.
3. **Domain question:** Log it in `docs/bitmap-cache/domain-questions.md` with a
   `// DOMAIN_QUESTION:` marker in the code. Make your best guess and continue.
4. **Unclear requirement:** Re-read this prompt. If still unclear, make a reasonable
   choice and document it in the PR description.
