# Implementation Plan: Variant Build Fix & Hash-Only Descriptor Store

**Branch:** `fix/variant-build-hash-only` from `dev`
**Date:** 2026-04-27
**Author:** Ian Gent & AI Assistant
**Fixes:** `known-issues.md` #11 (build.sh missing variant targets) and #14 (compact_state dual-role / hash-only payload waste)
**Adds:** `known-issues.md` #17 (byte-array descriptor store as future flat-cache optimisation)

---

## Background

Two bugs were identified in a diagnostic session on 2026-04-27:

1. **Build bug (#11):** `build.sh` never builds `solvitaire-flat`, `solvitaire-hash-only`, or
   `solvitaire-lru`. These are defined in CMakeLists.txt and required by all
   `regression_level*_flat`, `regression_level*_hash_only`, and `regression_level*_lru` CTest
   targets. Without the `--variants` flag, running the variant regression tests fails silently
   or uses stale binaries.

2. **Payload waste (#14):** `game_state` carries a `compact_state payload` member (32 bytes)
   under `SOLVITAIRE_COMPUTES_FLAT_HASH`. For the flat-cache path this is correct: the payload
   is the cache key copied into `flat_cache` clusters. For the hash-only path the payload is
   never copied into any cache cluster — only the 64-bit Zobrist hash is used. However, all
   four incremental update helpers (`update_card_descriptor`, `update_foundation_in_hash`,
   `update_waste_ptr_in_hash`, `update_hole_top_in_hash`) read the OLD value from `payload` and
   write the NEW value back to `payload` in order to compute the XOR delta. This means the
   entire 32-byte `compact_state` is maintained in every `game_state` on the DFS stack even in
   the hash-only binary, and `compact_state.h` is compiled into that binary unnecessarily.

   **Fix:** Introduce a lightweight `hash_descriptor_store` (plain byte arrays, no nibble
   packing) that serves as the old-value store for incremental Zobrist updates in the
   hash-only path. The `compact_state` type and header are excluded entirely from the
   `SOLVITAIRE_HASH_ONLY` compilation unit.

---

## Commit Sequence

### Commit 1 — `build: add --variants flag; build variant binaries in container`

**Files changed:** `build.sh`, `scripts/container-build.sh`, `Dockerfile`

#### `build.sh`

Replace the current rigid 1-or-2-argument parser with a loop-based parser that accepts
`--release`, `--debug`, `--solvitaire`, `--unit-tests`, and the new `--variants` flag in any
order (with mutual-exclusion checks).

New behaviour:
- `--variants`: after building the primary target, also build `solvitaire-flat`,
  `solvitaire-hash-only`, and `solvitaire-lru` via three additional `cmake --build` calls.
- All existing invocations (`./build.sh`, `./build.sh --release`, `./build.sh --release
  --unit-tests`, etc.) are unchanged.
- `--variants` and `--unit-tests` may be combined: `./build.sh --release --unit-tests
  --variants` builds main, unit_tests, and all three variant binaries.

Updated usage string:
```
Usage: ./build.sh [--release|--debug] [--solvitaire|--unit-tests] [--variants]
(default args = --release --solvitaire)
--variants: also build solvitaire-flat, solvitaire-hash-only, solvitaire-lru
```

**Implementation note:** The simplest rewrite parses all args in a `for arg in "$@"` loop
setting boolean flags (`build_variants=false` etc.), then acts after the loop. This replaces
the current if/elif chain and naturally handles 3-argument invocations.

#### `scripts/container-build.sh`

Add `--variants` to the case statement (sets `VARIANTS_FLAG="1"`). When set, the cmake build
command inside the container runs the three additional variant targets after the main build.
The container's Dockerfile already produces a single built image; variant binaries are built
at container-run time (not baked into the image), which is consistent with current `--test`
and `--regression` behaviour.

Add `--variants` to `print_usage` and the examples block.

#### `Dockerfile`

The Dockerfile bakes binaries into the image via `RUN ./build.sh --release`. Add a second
`RUN ./build.sh --release --variants` step so the variant binaries are present when
`ctest -R regression_level1_flat` etc. are run inside the image. This matches how
`./build.sh --release --unit-tests` is a separate step.

**Tests after this commit:**
```bash
./build.sh --release --variants
# Verify: cmake-build-release/solvitaire-flat, solvitaire-hash-only, solvitaire-lru exist
ls cmake-build-release/solvitaire-{flat,hash-only,lru}
```

---

### Commit 2 — `descriptor: extract card_descriptor enum to shared header`

**Files changed:** `src/main/game/descriptor.h` (new),
`src/main/game/compact_state.h`,
`src/main/game/search-state/game_state.cpp`,
`src/test/unit_tests/zobrist_test.cpp`

**Purpose:** Decouple the descriptor enum constants from `compact_state` so that
`game_state.cpp` can use `card_descriptor::STARTING` etc. without requiring `compact_state.h`
to be in scope — which is necessary for the hash-only path in Commit 4.

#### New file: `src/main/game/descriptor.h`

```cpp
#ifndef SOLVITAIRE_DESCRIPTOR_H
#define SOLVITAIRE_DESCRIPTOR_H

#include <cstdint>

// Per-card descriptor values used by the Zobrist hash and flat cache payload.
// Shared between compact_state (flat cache path) and hash_descriptor_store
// (hash-only path) so neither path needs to include the other's header.
enum card_descriptor : uint8_t {
    STARTING         = 0,  // Face-down in original position; also reused for foundation cards
    STARTING_FACE_UP = 1,  // Originally face-down, now revealed, not yet moved, not at pile bottom
    ROOT             = 2,  // At pile bottom with no legal-build parent (parent_table fallback)
    IN_CELL          = 3,  // In a free cell
    PARENT_0         = 4,  // Built on first legal parent (fixed suit ordering)
    PARENT_1         = 5,  // Built on second legal parent
    PARENT_2         = 6,  // Built on third legal parent
    PARENT_3         = 7,  // Built on fourth legal parent
    IN_HOLE          = 8,  // Played to the hole (hole games only)
    IN_SPACE         = 9,  // At the bottom of a tableau pile (nothing below, or only face-down)
    // 10–15 reserved
};

#endif // SOLVITAIRE_DESCRIPTOR_H
```

#### `src/main/game/compact_state.h`

1. Add `#include "descriptor.h"` near the top.
2. Remove the existing nested `enum descriptor : uint8_t { ... }` block from the struct.
3. Inside the struct, add a backward-compatibility type alias so `compact_state::descriptor`
   still names the enum type, and add `static constexpr` members for each value so that
   `compact_state::STARTING` etc. continue to compile in all flat-path code (including
   `zobrist_test.cpp` which uses `compact_state::STARTING` heavily):

```cpp
// Backward-compatibility aliases — flat-cache code outside game_state.cpp may use these.
using descriptor = card_descriptor;
static constexpr card_descriptor STARTING         = card_descriptor::STARTING;
static constexpr card_descriptor STARTING_FACE_UP = card_descriptor::STARTING_FACE_UP;
static constexpr card_descriptor ROOT             = card_descriptor::ROOT;
static constexpr card_descriptor IN_CELL          = card_descriptor::IN_CELL;
static constexpr card_descriptor PARENT_0         = card_descriptor::PARENT_0;
static constexpr card_descriptor PARENT_1         = card_descriptor::PARENT_1;
static constexpr card_descriptor PARENT_2         = card_descriptor::PARENT_2;
static constexpr card_descriptor PARENT_3         = card_descriptor::PARENT_3;
static constexpr card_descriptor IN_HOLE          = card_descriptor::IN_HOLE;
static constexpr card_descriptor IN_SPACE         = card_descriptor::IN_SPACE;
```

This preserves `compact_state::STARTING` in `zobrist_test.cpp` and
`generic_flat_cache_policies.h` without touching those files.

#### `src/main/game/search-state/game_state.cpp`

Replace all ~30 occurrences of `compact_state::STARTING`, `compact_state::ROOT`, etc. with
`card_descriptor::STARTING`, `card_descriptor::ROOT`, etc.

**Full list of replacements** (all in `game_state.cpp`):

| Old | New |
|-----|-----|
| `compact_state::STARTING` | `card_descriptor::STARTING` |
| `compact_state::STARTING_FACE_UP` | `card_descriptor::STARTING_FACE_UP` |
| `compact_state::ROOT` | `card_descriptor::ROOT` |
| `compact_state::IN_CELL` | `card_descriptor::IN_CELL` |
| `compact_state::PARENT_0` through `PARENT_3` | `card_descriptor::PARENT_0` etc. |
| `compact_state::IN_HOLE` | `card_descriptor::IN_HOLE` |
| `compact_state::IN_SPACE` | `card_descriptor::IN_SPACE` |

Do NOT change `payload.get_descriptor(...)`, `payload.set_descriptor(...)`, or
`compact_state payload` — those are addressed in Commit 4.

`game_state.cpp` still includes `compact_state.h` at this stage (that conditionalization
happens in Commit 4). The only change here is the enum-value references.

#### `src/test/unit_tests/zobrist_test.cpp`

No change needed. `zobrist_test.cpp` uses `compact_state::STARTING` etc. throughout; the
backward-compat `static constexpr` members added to `compact_state.h` in this commit preserve
those references. The test file always compiles with `compact_state.h` included.

**Tests after this commit:**
```bash
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R unit_tests --output-on-failure
# All unit tests must pass — this is a pure refactor
```

---

### Commit 3 — `hash_descriptor_store: new lightweight descriptor store for hash-only path`

**Files changed:** `src/main/game/hash_descriptor_store.h` (new)

#### New file: `src/main/game/hash_descriptor_store.h`

A header-only struct. No `.cpp` file needed — all methods are trivial one-liners.

```cpp
#ifndef SOLVITAIRE_HASH_DESCRIPTOR_STORE_H
#define SOLVITAIRE_HASH_DESCRIPTOR_STORE_H

#include <cstdint>
#include <cstring>
#include "descriptor.h"

// Lightweight old-value store for incremental Zobrist hash maintenance in the
// hash-only cache path. Replaces compact_state in SOLVITAIRE_HASH_ONLY builds.
//
// Unlike compact_state, descriptors are stored as a plain byte array (one byte
// per card, no nibble packing). Getters and setters are direct array accesses
// with no bit operations. Foundation ranks, waste pointer, and hole top are
// also plain bytes.
//
// This struct is NOT a cache key and is never copied into cache clusters.
// It exists solely on the DFS stack as old-value storage for XOR delta
// computation in the four update_*_in_hash helpers.
//
// Note: 52 bytes for descriptors vs 26 bytes (nibble-packed) in compact_state.
// The extra 26 bytes per game_state on the DFS stack is negligible — the DFS
// stack is at most ~200 deep, giving ~5 KB extra vs millions of cache cluster
// copies in compact_state format. See known-issues.md #17 for a future
// optimisation that could apply the same byte-array approach to the flat cache.
struct hash_descriptor_store {
    uint8_t desc[52];       // descriptor per card; index = card_id (0–51)
    uint8_t foundations[4]; // top rank per suit (0 = empty)
    uint8_t waste_ptr;      // effective waste pointer (0–63)
    uint8_t hole_top;       // card_id of hole top (0 = none)

    void clear() {
        std::memset(desc, card_descriptor::STARTING, 52);
        std::memset(foundations, 0, 4);
        waste_ptr = 0;
        hole_top  = 0;
    }

    uint8_t get_descriptor(uint8_t card_id) const { return desc[card_id]; }
    void    set_descriptor(uint8_t card_id, uint8_t value) { desc[card_id] = value; }

    uint8_t get_foundation(uint8_t suit) const { return foundations[suit]; }
    void    set_foundation(uint8_t suit, uint8_t rank) { foundations[suit] = rank; }

    uint8_t get_waste_ptr() const { return waste_ptr; }
    void    set_waste_ptr(uint8_t ptr) { waste_ptr = ptr; }

    uint8_t get_hole_top() const { return hole_top; }
    void    set_hole_top(uint8_t cid) { hole_top = cid; }
};

#endif // SOLVITAIRE_HASH_DESCRIPTOR_STORE_H
```

**Tests after this commit:**
```bash
./build.sh --release
# Build must succeed (new header, not yet wired in)
```

---

### Commit 4 — `game_state: use hash_descriptor_store in SOLVITAIRE_HASH_ONLY; exclude compact_state`

**Files changed:**
`src/main/game/search-state/game_state.h`,
`src/main/game/search-state/game_state.cpp`

This is the functional commit. All previous commits are preparatory.

#### `src/main/game/search-state/game_state.h`

**Includes section** (around line 53):

Replace the unconditional `#include "../compact_state.h"` with:

```cpp
#include "descriptor.h"
#ifndef SOLVITAIRE_HASH_ONLY
#  include "../compact_state.h"
#else
#  include "../hash_descriptor_store.h"
#endif
```

`descriptor.h` is unconditionally included (needed for `card_descriptor::*` constants on all
paths).

**Member declarations** (around lines 210–213, inside `#if SOLVITAIRE_COMPUTES_FLAT_HASH`):

Replace:
```cpp
    compact_state payload;
```
with:
```cpp
#ifdef SOLVITAIRE_HASH_ONLY
    hash_descriptor_store hash_desc;
#else
    compact_state payload;
#endif
```

**`get_payload()` accessor** (line 101):

Wrap in `#ifndef SOLVITAIRE_HASH_ONLY` so it only exists for the flat path (it returns
`const compact_state&` which doesn't exist in the hash-only binary):

```cpp
#if SOLVITAIRE_COMPUTES_FLAT_HASH && !defined(SOLVITAIRE_HASH_ONLY)
    const compact_state& get_payload() const { return payload; }
    void set_payload_depth(uint16_t depth);
    void compute_hash_from_scratch();
#endif
```

**Debug methods** (lines 112–115): wrap in `#if SOLVITAIRE_COMPUTES_FLAT_HASH &&
!defined(SOLVITAIRE_HASH_ONLY)` — `recompute_payload_from_scratch()` and
`assert_payload_consistent()` are payload-specific and have no meaning in the hash-only
path.

**`computing_flat_payload` flag:** This member is under `#if SOLVITAIRE_COMPUTES_FLAT_HASH`.
For hash-only it is always `false` and the code already guards on it correctly. Leave it in
place — it costs only 1 byte and removing it would require auditing more call sites than
this commit warrants.

#### `src/main/game/search-state/game_state.cpp`

Four update helpers — add `#ifdef SOLVITAIRE_HASH_ONLY` / `#else` / `#endif` dispatch inside
each `if (computing_flat_hash)` block. The structure for each is:

```cpp
void game_state::update_card_descriptor(uint8_t cid, uint8_t new_desc) {
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    if (computing_flat_hash) {
#ifdef SOLVITAIRE_HASH_ONLY
        uint8_t old_desc = hash_desc.get_descriptor(cid);
        hash_desc.set_descriptor(cid, new_desc);
#else
        uint8_t old_desc = payload.get_descriptor(cid);
        payload.set_descriptor(cid, new_desc);
#endif
        zobrist_hash_value ^= zobrist_hash::card_key(cid, old_desc)
                            ^ zobrist_hash::card_key(cid, new_desc);
    }
#else
    (void)cid; (void)new_desc;
#endif
}
```

Apply the same `#ifdef SOLVITAIRE_HASH_ONLY` / `#else` dispatch to:
- `update_foundation_in_hash`: `hash_desc.get/set_foundation` vs `payload.get/set_foundation`
- `update_waste_ptr_in_hash`: `hash_desc.get/set_waste_ptr` vs `payload.get/set_waste_ptr`
- `update_hole_top_in_hash`: `hash_desc.get/set_hole_top` vs `payload.get/set_hole_top`

`init_payload_and_hash()`: add dispatch for `clear()` and the foundation/hole/waste init
sections:

```cpp
void game_state::init_payload_and_hash() {
    if (!computing_flat_hash) return;
#ifdef SOLVITAIRE_HASH_ONLY
    hash_desc.clear();
#else
    payload.clear();
#endif
    zobrist_hash_value = 0;
    // ... rest of function uses update_card_descriptor / update_foundation_in_hash etc.
    //     which already dispatch correctly; no further changes needed in the body
```

The `payload.set_foundation(s, top_rank)` and similar direct `payload.*` calls in the body of
`init_payload_and_hash` (lines ~1162, 1171, 1181) must also be dispatched:

```cpp
#ifdef SOLVITAIRE_HASH_ONLY
    hash_desc.set_foundation(s, top_rank);
#else
    payload.set_foundation(s, top_rank);
#endif
    zobrist_hash_value ^= zobrist_hash::foundation_key(s, top_rank);
```

(These `payload.*` direct calls in `init_payload_and_hash` are separate from the incremental
`update_foundation_in_hash` helper — both must be dispatched.)

**`compute_hash_from_scratch()`** (line 1362): guarded by `!defined(SOLVITAIRE_HASH_ONLY)`
via the header change; the definition in `.cpp` should be wrapped the same way so it is not
compiled in hash-only builds.

**Line 1137** — `payload.get_descriptor(cid) == compact_state::STARTING`:
```cpp
#ifdef SOLVITAIRE_HASH_ONLY
    if (computing_flat_hash && hash_desc.get_descriptor(cid) == card_descriptor::STARTING) {
#else
    if (computing_flat_hash && payload.get_descriptor(cid) == card_descriptor::STARTING) {
#endif
```

**`#include "../compact_state.h"`** in `game_state.cpp`: this is already transitively pulled
in via `game_state.h`; if `game_state.cpp` has its own explicit include, conditionalize it
to match the header.

**Tests after this commit:**
```bash
# Build all three variant binaries
./build.sh --release --variants

# Unit tests (flat-cache path, includes get_payload()-based tests)
cd cmake-build-release && ctest -R unit_tests --output-on-failure

# Level 1 regression for all three variants
cd cmake-build-release && ctest -R regression_level1_flat --output-on-failure
cd cmake-build-release && ctest -R regression_level1_hash_only --output-on-failure
cd cmake-build-release && ctest -R regression_level1_lru --output-on-failure

# Main binary Level 1 (must not regress)
cd cmake-build-release && ctest -R regression_level1$ --output-on-failure
```

---

### Commit 5 — `docs: close known-issues #11 and #14; add #17`

**Files changed:** `docs/known-issues.md`

- **Close #11** (build script missing variant binaries): mark Resolved, reference Commit 1.
- **Close #14** (descriptor state tracked only inside `compact_state payload`): mark Resolved,
  reference Commit 4. The `hash_descriptor_store` is the "separate compact descriptor-tracking
  array" the issue called for.
- **Add #17** (byte-array descriptor store as future flat-cache optimisation):

```
### 17. Byte-Array Descriptor Store Not Yet Used on Flat-Cache Path

**Status:** Open; deferred post-merge optimisation
**Impact:** Potential performance — flat-cache path could avoid nibble bit-operations on
every Zobrist update

hash_descriptor_store (introduced by fix/variant-build-hash-only) stores descriptors as a
plain byte array (one byte per card). compact_state packs them as nibbles (4 bits per card,
26 bytes for 52 cards). Every call to get_descriptor / set_descriptor in the flat-cache path
requires a shift and mask. For the flat cache these nibble operations are on the hot Zobrist
update path, executed millions of times per solve.

Using hash_descriptor_store (or a similar byte-array store) for the flat-cache path too
would eliminate these bit operations. The trade-off is 26 extra bytes per game_state on the
DFS stack (52 bytes vs 26), and that the flat cache clusters still use compact_state format
(unchanged), so the descriptor store and the cache payload would be different types — the
game_state update path would write to hash_descriptor_store, and the cache-insert path would
separately copy into a compact_state for the cache key.

Magnitude TBD — benchmark before acting. Only worth doing if profiling shows nibble
operations are a measurable fraction of total solve time.
```

---

## Testing Protocol

Each commit must pass before the next begins. The sign-off tests are listed per-commit above.
The full regression suite is run only after Commit 4:

```bash
./build.sh --release --variants --unit-tests
cd cmake-build-release
ctest -R unit_tests --output-on-failure
ctest -R regression_level1 --output-on-failure         # all four: main, flat, hash_only, lru
ctest -R regression_level2 --output-on-failure         # all four variants
```

Level 3–5 regressions are optional for this branch given the fixes are structural/build-only
and hash-only node counts are already validated in existing oracles.

---

## Session Log

| Date | Session | Commits completed | Notes |
|------|---------|-------------------|-------|
| 2026-04-27 | Planning | — | Plan written; branch not yet created |
| 2026-04-27 | Implementation | Commit 1 (0653486) | build.sh, container-build.sh; Dockerfile already built variants, no change needed |

**Rule: update PICKUP.md and this session log at the end of every session in which a commit
is completed.**
