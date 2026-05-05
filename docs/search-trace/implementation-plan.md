# Search Trace: Implementation Plan

**Date:** 2026-05-05  
**Branch:** `feature/search-trace` (from `dev`)  
**Design doc:** `docs/proposals/PROPOSAL-search-trace.md`  
**Status:** Ready to implement

---

## Overview

Four commits. The first two land on `dev` and produce the reference binaries used for
merge validation. The branch is then merged into `feature/templated-dispatch`, where the
comparison binaries are built and the validation is run.

```
dev:                       [A] [B] [C] [D] ──── (reference builds saved here)
                                              \
feature/templated-dispatch:                   merge ── (comparison builds, validate, merge to dev)
```

---

## Testing Gate (all commits)

Each commit must pass before proceeding:

```bash
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R unit_tests --output-on-failure
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
```

Commits B and C additionally require a debug build gate:

```bash
./build.sh --debug --unit-tests
cd cmake-build-debug && ctest -R unit_tests --output-on-failure
```

---

## Commit A: `trace_writer` skeleton + CMake wiring

**Goal:** Build infrastructure in place; all macros defined; no callsites yet. Trace
compiles cleanly in both debug (enabled) and release (disabled) builds.

### Files changed

**New: `src/main/solver/search_trace.h`**

Full header with:
- Disabled-path macros (all expand to `((void)0)`)
- Enabled-path macros (forward to `trace_writer::instance().*` methods)
- `trace_writer` class declaration (singleton, `open`, `close`, `enabled`, `set_break_at`,
  all `write_*` methods)
- Comment documenting the lazy-evaluation assumption for macro arguments

```cpp
#ifndef SOLVITAIRE_SEARCH_TRACE_H
#define SOLVITAIRE_SEARCH_TRACE_H

#include "../game/move.h"
#include <cstdint>
#include <string>

#ifdef SOLVITAIRE_SEARCH_TRACE

class trace_writer {
public:
    static trace_writer& instance();
    void open(const std::string& path, int argc, char** argv);
    void close();
    bool enabled() const { return enabled_; }
    void set_break_at(uint64_t n) { break_at_ = n; }
    void write_init(const std::string& game_type, int seed,
                    const std::string& streamliner, const std::string& policy);
    void write_move(const move& mv);
    void write_undo(const move& mv);
    void write_depth(uint64_t d);
    void write_event(const char* keyword);
    void write_legal(std::size_t n);
    // break-at support: called after each write_line; needs game_state ptr when break fires
    void set_break_state_printer(std::function<void()> printer);
private:
    FILE*    file_     = nullptr;
    uint64_t op_       = 0;
    bool     enabled_  = false;
    uint64_t break_at_ = UINT64_MAX;
    std::function<void()> break_printer_;
    void write_line(const char* fmt, ...);
};

// Argument note: all macro arguments are simple values (move struct, integrals).
// No expensive expressions appear at callsites; argument evaluation when
// SOLVITAIRE_SEARCH_TRACE is undefined is not a practical concern.
#define STRACE_INIT(type, seed, streamliner, policy) \
    trace_writer::instance().write_init(type, seed, streamliner, policy)
#define STRACE_MOVE(mv)    trace_writer::instance().write_move(mv)
#define STRACE_UNDO(mv)    trace_writer::instance().write_undo(mv)
#define STRACE_DEPTH(d)    trace_writer::instance().write_depth(d)
#define STRACE_QUERY()     trace_writer::instance().write_event("QUERY")
#define STRACE_HIT()       trace_writer::instance().write_event("HIT")
#define STRACE_MISS()      trace_writer::instance().write_event("MISS")
#define STRACE_INSERT()    trace_writer::instance().write_event("INSERT")
#define STRACE_EVICT()     trace_writer::instance().write_event("EVICT")
#define STRACE_LEGAL(n)    trace_writer::instance().write_legal(n)
#define STRACE_RESULT(r)   trace_writer::instance().write_event(r)

#else  // SOLVITAIRE_SEARCH_TRACE not defined — zero overhead

#define STRACE_INIT(type, seed, streamliner, policy) ((void)0)
#define STRACE_MOVE(mv)    ((void)0)
#define STRACE_UNDO(mv)    ((void)0)
#define STRACE_DEPTH(d)    ((void)0)
#define STRACE_QUERY()     ((void)0)
#define STRACE_HIT()       ((void)0)
#define STRACE_MISS()      ((void)0)
#define STRACE_INSERT()    ((void)0)
#define STRACE_EVICT()     ((void)0)
#define STRACE_LEGAL(n)    ((void)0)
#define STRACE_RESULT(r)   ((void)0)

#endif  // SOLVITAIRE_SEARCH_TRACE
#endif  // SOLVITAIRE_SEARCH_TRACE_H
```

**New: `src/main/solver/search_trace.cpp`**

Full implementation:
- Meyer's singleton
- `open`: create file, write 5-line header (TRACE, DATE, CMD, GAME placeholder, POLICY
  placeholder) + blank line. DATE from `std::time` formatted as ISO 8601. CMD reconstructed
  from `argc`/`argv`.
- `write_init`: writes `GAME` and `POLICY` lines. Called after dispatch in `main.cpp` once
  game type and policy are known. (Header is written by `open`; these two lines are written
  immediately after. Alternative: buffer them and flush at `write_init` — simpler.)
- `write_line`: `snprintf` into `char buf[256]`; prepend `%010llu ` counter; `fwrite`;
  check `break_at_`.
- `write_move` / `write_undo`: format move fields in fixed order
  `t=X f=X to=X c=X rev=X flip=X dom=X`.
- `write_depth`, `write_legal`: format single integer field.
- `write_event`: write keyword only (QUERY, HIT, MISS, INSERT, EVICT, SOLVED, etc.)
- `close`: `fclose(file_)`, set `file_ = nullptr`.

**Modified: `CMakeLists.txt`**

Add after the existing debug/release flag setup:

```cmake
# Search trace — on by default for debug builds, off for release
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SOLVITAIRE_TRACE_DEFAULT ON)
else()
    set(SOLVITAIRE_TRACE_DEFAULT OFF)
endif()
option(SOLVITAIRE_TRACE "Enable search trace instrumentation" ${SOLVITAIRE_TRACE_DEFAULT})
if(SOLVITAIRE_TRACE)
    message(STATUS "Search trace: ENABLED")
    foreach(tgt solvitaire solvitaire-flat solvitaire-hash-only solvitaire-lru)
        if(TARGET ${tgt})
            target_compile_definitions(${tgt} PRIVATE SOLVITAIRE_SEARCH_TRACE)
            target_sources(${tgt} PRIVATE
                ${CMAKE_SOURCE_DIR}/src/main/solver/search_trace.cpp)
        endif()
    endforeach()
else()
    message(STATUS "Search trace: disabled")
endif()
```

Also add `search_trace.cpp` to the `unit_tests` target (needed for Commit C tests).

### Testing gate for Commit A

- Release build clean, no new warnings
- Debug build clean, no new warnings
- Unit tests pass (no new tests yet — just verifying nothing breaks)
- Regression level 1 passes

---

## Commit B: Callsites in solver and caches

**Goal:** All trace events fire at the right points in the DFS loop and cache
implementations.

### Files changed

**Modified: `src/main/solver/solver.cpp`**

Add `#include "search_trace.h"` at top.

Insert macros at the following locations (line numbers approximate; verify against current
file before editing):

```cpp
// ~line 178: after make_move
state.make_move(current_node->mv);
STRACE_MOVE(current_node->mv);
res.depth++;
STRACE_DEPTH(res.depth);

// ~line 127: cache insert — flat path
STRACE_QUERY();
is_new_state = cache.insert_t(state);
if (is_new_state) { STRACE_MISS(); STRACE_INSERT(); }
else              { STRACE_HIT(); }

// ~line 144: cache insert — LRU path
STRACE_QUERY();
pair<...> insert_res = cache.insert_with_iterator(state);
...
if (insert_res.second) { STRACE_MISS(); STRACE_INSERT(); }
else                   { STRACE_HIT(); }

// ~line 151: legal moves
vector<move> next_moves = state.get_legal_moves(current_node->mv);
STRACE_LEGAL(next_moves.size());

// ~line 215: after undo_move
state.undo_move(current_node->mv);
STRACE_UNDO(current_node->mv);
res.depth--;
STRACE_DEPTH(res.depth);

// ~line 188-194: solver result
if (state.is_solved())  { STRACE_RESULT("SOLVED"); }
else                    { STRACE_RESULT("UNSOLV"); }
// and in the timeout/terminated returns:
return result::type::TIMEOUT;   // add STRACE_RESULT("TIMEOUT") before each early return
```

**Modified: `src/main/game/generic_flat_cache.h`**

Add `#include "../../solver/search_trace.h"` at top.

Insert `STRACE_EVICT();` at each `++eviction_count;` site (there are currently 5, at
lines ~194, ~220, ~222, ~259, ~264 — verify against current file).

**Modified: `src/main/game/global_cache.cpp`**

Add `#include "../solver/search_trace.h"` and insert `STRACE_EVICT();` at the LRU
capacity-eviction site (where an entry is removed from the Boost MultiIndex list to make
room — distinct from `set_non_live`).

### Testing gate for Commit B

- Debug build: `./solvitaire --type klondike --random 42 --trace /tmp/test.trace` produces
  a non-empty file beginning with the correct 5-line header
- Verify by inspection that the event sequence for a trivial game looks correct
- Full unit tests + regression level 1 pass in both debug and release

---

## Commit C: CLI flags and break-at-N

**Goal:** `--trace` and `--trace-break-at` wired end-to-end.

### Files changed

**Modified: `src/main/input-output/input/command_line_helper.h/.cpp`**

Add to `main_options.add_options()`:

```cpp
("trace", po::value<string>(),
    "write search trace to file (requires SOLVITAIRE_SEARCH_TRACE build)")
("trace-break-at", po::value<uint64_t>(),
    "re-run and print game state at operation N (requires --trace)")
```

**Modified: `src/main/main.cpp`**

After parsing CLI args and before dispatching to solver:

```cpp
#ifdef SOLVITAIRE_SEARCH_TRACE
    if (vm.count("trace")) {
        trace_writer::instance().open(vm["trace"].as<string>(), argc, argv);
    } else if (vm.count("trace-break-at")) {
        std::cerr << "Warning: --trace-break-at requires --trace\n";
    }
    if (vm.count("trace-break-at")) {
        trace_writer::instance().set_break_at(vm["trace-break-at"].as<uint64_t>());
    }
#else
    if (vm.count("trace") || vm.count("trace-break-at")) {
        std::cerr << "Warning: --trace ignored (binary not built with SOLVITAIRE_TRACE)\n";
    }
#endif
```

`STRACE_INIT` is called from inside `dispatch_solve()` in `main.cpp`, once the game type,
seed, streamliner, and policy are all known — i.e., inside each dispatch branch just
before the solver is constructed:

```cpp
// Inside dispatch_solve(), flat branch example:
STRACE_INIT(rules.game_type_name(), seed, streamliner_str, "flat");
solver_impl<FlatPolicy> solver(...);
```

**Break-at-N wiring:**

The `break_at_` check in `write_line` needs to print the current game state. Since
`trace_writer` cannot directly access `game_state` (wrong abstraction level), the solver
registers a printer lambda when the break value is set:

```cpp
// In solver_impl constructor (or solve() method), if break_at set:
#ifdef SOLVITAIRE_SEARCH_TRACE
    if (trace_writer::instance().enabled()) {
        trace_writer::instance().set_break_state_printer(
            [this]() { std::cout << state; }
        );
    }
#endif
```

### Testing gate for Commit C

- `./cmake-build-debug/solvitaire --type klondike --random 1 --trace /tmp/t.trace` —
  file created, header correct, events present
- `diff <(./cmake-build-debug/solvitaire --type klondike --random 1 --trace /tmp/a.trace && tail -n +6 /tmp/a.trace) <(./cmake-build-debug/solvitaire --type klondike --random 1 --trace /tmp/b.trace && tail -n +6 /tmp/b.trace)` — zero diff (same binary, same seed)
- `--trace-break-at` with a known-small operation count prints game state and exits
- Warning printed when `--trace` used with a release binary
- Full unit tests + regression level 1 pass

---

## Commit D: Tests

**Goal:** Formal unit tests and integration test infrastructure in place.

### Files changed

**New: `src/test/unit_tests/search_trace_test.cpp`**

Four GoogleTest cases (require `SOLVITAIRE_SEARCH_TRACE` defined — skip gracefully if
not, via `GTEST_SKIP()`):

1. **`TraceWriter.HeaderFormat`** — call `open()` + `write_init()` on a temp file; read
   back and verify the 5 header lines match expected format including `TRACE v=1`, `DATE`
   prefix, `CMD` prefix, `GAME`, `POLICY`, blank line.

2. **`TraceWriter.EventOrdering`** — solve a minimal trivial instance (e.g., Black Hole
   seed that solves in <10 moves); capture trace to temp file; parse event keywords in
   order; assert the sequence matches the documented ordering (DEPTH, MOVE, QUERY, MISS,
   INSERT, LEGAL, ..., UNDO, DEPTH, SOLVED).

3. **`TraceWriter.MonotonicCounter`** — generate a short trace; parse all operation
   numbers; assert each is exactly previous + 1 with no gaps.

4. **`TraceWriter.NoOpWhenDisabled`** — construct a `trace_writer` without calling
   `open()`; call several `write_*` methods; assert `enabled()` returns false and no file
   was created.

**New: `scripts/compare_traces.py`**

Python 3 script. Key features:
- Strips header (first 5 lines + blank = `tail -n +6` equivalent)
- Modes: `--full` (exact diff), `--until-evict` (truncate both at first EVICT line),
  `--until-timeout N` (truncate both at operation N)
- `--binary-a` / `--binary-b`: invoke each binary with `--trace` to a temp file, then
  compare
- `--binary` (shorthand): run same binary twice, assert identity
- Exits 0 on match, 1 on divergence; prints first differing line number and operation
  number on divergence

**Modified: `CMakeLists.txt`**

Add four CTest targets (only registered when `SOLVITAIRE_TRACE` is ON and variant
binaries exist):

```cmake
if(SOLVITAIRE_TRACE)
    add_test(NAME trace_identity_flat
        COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/compare_traces.py
            --binary $<TARGET_FILE:solvitaire-flat>
            --game freecell --seed 1)

    add_test(NAME trace_identity_lru
        COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/compare_traces.py
            --binary $<TARGET_FILE:solvitaire-lru>
            --game klondike --seed 1)

    add_test(NAME trace_until_eviction
        COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/compare_traces.py
            --until-evict
            --binary-a $<TARGET_FILE:solvitaire-flat>
            --binary-b $<TARGET_FILE:solvitaire-lru>
            --game freecell --seed 1)

    add_test(NAME trace_until_timeout
        COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/compare_traces.py
            --until-timeout 10000
            --binary $<TARGET_FILE:solvitaire-flat>
            --game klondike --seed 99)
endif()
```

### Testing gate for Commit D

- All four GoogleTest trace cases pass in debug build
- All four CTest trace targets pass
- Full unit tests + regression level 1 pass in both debug and release
- `compare_traces.py --binary solvitaire-flat --game freecell --seed 1` exits 0

---

## After Commit D: Reference Builds on `dev`

Once all four commits are merged to `dev`, build and save the reference trace-enabled
binaries before merging `dev` into `feature/templated-dispatch`:

```bash
# On dev branch, after merging Commits A-D
./build.sh --release          # builds solvitaire (tracing OFF in release)
./build.sh --debug            # builds solvitaire with tracing ON
./build.sh --release --variants  # builds solvitaire-flat, -hash-only, -lru (tracing OFF)

# Explicitly build trace-enabled release variant binaries for comparison
cmake -DCMAKE_BUILD_TYPE=Release -DSOLVITAIRE_TRACE=ON -B cmake-build-trace
cmake --build cmake-build-trace --target solvitaire-flat solvitaire-hash-only solvitaire-lru

# Save reference copies
mkdir -p reference-builds/dev-pre-merge
cp cmake-build-trace/solvitaire-flat     reference-builds/dev-pre-merge/
cp cmake-build-trace/solvitaire-hash-only reference-builds/dev-pre-merge/
cp cmake-build-trace/solvitaire-lru      reference-builds/dev-pre-merge/
```

Then follow `docs/search-trace/merge-validation-procedure.md` (written in a follow-on
commit after reference builds are confirmed working).

---

## Merge Validation Procedure (outline)

Full details in `merge-validation-procedure.md`. Brief outline:

1. Merge `dev` (with tracing) into `feature/templated-dispatch`.
2. Build trace-enabled variant binaries from the merged branch.
3. For each validation game+seed pair (suggested: FreeCell seeds 1-5, Klondike seeds 1-5,
   Black Hole seeds 1-5 — short runs, guaranteed to complete):
   ```bash
   compare_traces.py \
       --binary-a reference-builds/dev-pre-merge/solvitaire-flat \
       --binary-b cmake-build-trace/solvitaire-flat \
       --game freecell --seed N
   ```
4. Repeat for `hash-only` and `lru` variants.
5. All comparisons exit 0 → merge is safe.

---

## Constraints

- **One commit per session** (standard project rule).
- **Stop and report on any test failure** — do not debug.
- **Stop and report on any domain question** about game semantics or event ordering.
- The `STRACE_EVICT` placement in `generic_flat_cache.h` must match actual eviction
  sites exactly — verify by inspection against the current file before editing.
- The break-at-N printer lambda requires care: it captures `this` in `solver_impl`, which
  is templated. Verify the lambda compiles for all four Policy instantiations.
