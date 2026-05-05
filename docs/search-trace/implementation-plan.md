# Search Trace: Implementation Plan

**Date:** 2026-05-05  
**Branch:** `feature/search-trace` (from `dev`)  
**Design doc:** `docs/proposals/PROPOSAL-search-trace.md` (on `feature/templated-dispatch`)  
**Status:** Commit A complete

---

## Overview

Four commits. The first four land on `dev` via this branch and produce the reference
binaries used for merge validation of `feature/templated-dispatch`.

```
dev:                       [A] [B] [C] [D] ──── (reference builds saved here)
                                              \
feature/templated-dispatch:                   merge ── (comparison builds, validate, merge to dev)
```

---

## Testing Gate (all commits)

```bash
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R ^unit_tests$ --output-on-failure
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
```

**Note:** Use `^unit_tests$` (anchored) — the unanchored form also matches
`unit_tests_full` (~5 min). Commits B and C additionally require a debug build gate:

```bash
./build.sh --debug --unit-tests
cd cmake-build-debug && ctest -R ^unit_tests$ --output-on-failure
```

---

## Commit A: `trace_writer` skeleton + CMake wiring — DONE (d2b2c09)

- `src/main/solver/search_trace.h` — class declaration + all macros
- `src/main/solver/search_trace.cpp` — full implementation (no callsites yet)
- `CMakeLists.txt` — `SOLVITAIRE_TRACE` option, sources, target definitions

---

## Commit B: Callsites in solver and caches

**Goal:** All trace events fire at the right points in the DFS loop and cache
implementations. No new files — only edits to existing source.

### Files to change

**`src/main/solver/solver.cpp`** — add `#include "search_trace.h"` and insert:

```cpp
// After make_move (~line 179):
state.make_move(current_node->mv);
STRACE_MOVE(current_node->mv);
res.depth++;
STRACE_DEPTH(res.depth);

// Cache insert — flat path (~line 127), before/after insert_t:
STRACE_QUERY();
is_new_state = cache.insert_t(state);
if (is_new_state) { STRACE_MISS(); STRACE_INSERT(); }
else              { STRACE_HIT(); }

// Cache insert — LRU path (~line 144), same pattern around insert_with_iterator

// Legal moves (~line 152):
vector<move> next_moves = state.get_legal_moves(current_node->mv);
STRACE_LEGAL(next_moves.size());

// After undo_move (~line 215):
state.undo_move(current_node->mv);
STRACE_UNDO(current_node->mv);
res.depth--;
STRACE_DEPTH(res.depth);

// Before each return in dfs():
STRACE_RESULT("SOLVED");   // or "UNSOLV" / "TIMEOUT" / "TERMINATED"
```

**`src/main/game/generic_flat_cache.h`** — add `#include "../../solver/search_trace.h"`
and `STRACE_EVICT();` at each `++eviction_count;` site (currently 5 sites).

**`src/main/game/global_cache.cpp`** — add include and `STRACE_EVICT();` at the
LRU capacity-eviction site (where an entry is removed from the Boost MultiIndex list
to make room — distinct from `set_non_live`).

### Verification

After building debug, run manually and inspect the trace file:
```bash
./cmake-build-debug/solvitaire --type black-hole --random 1 --trace /tmp/test.trace
head -20 /tmp/test.trace   # verify header
tail -20 /tmp/test.trace   # verify events and result
```

---

## Commit C: CLI flags and break-at-N

**Goal:** `--trace` and `--trace-break-at` wired end-to-end.

### Files to change

**`src/main/input-output/input/command_line_helper.cpp`** — add to `add_options()`:
```cpp
("trace", po::value<string>(),
    "write search trace to file (requires SOLVITAIRE_SEARCH_TRACE build)")
("trace-break-at", po::value<uint64_t>(),
    "re-run and print game state at operation N (requires --trace)")
```

**`src/main/main.cpp`** — after CLI parsing, before dispatch:
```cpp
#ifdef SOLVITAIRE_SEARCH_TRACE
    if (vm.count("trace")) {
        trace_writer::instance().open(vm["trace"].as<string>(), argc, argv);
    }
    if (vm.count("trace-break-at")) {
        trace_writer::instance().set_break_at(vm["trace-break-at"].as<uint64_t>());
    }
#else
    if (vm.count("trace") || vm.count("trace-break-at")) {
        std::cerr << "Warning: --trace ignored (not built with SOLVITAIRE_TRACE)\n";
    }
#endif
```

Also call `STRACE_INIT(...)` inside each dispatch branch in `dispatch_solve()`, once
game type and policy are known, just before the solver is constructed.

**Break-at-N game state wiring** — in `solver_impl` constructor (or `solve()`), register
the printer lambda:
```cpp
#ifdef SOLVITAIRE_SEARCH_TRACE
    if (trace_writer::instance().enabled()) {
        trace_writer::instance().set_break_state_printer(
            [this]() { std::cout << state; }
        );
    }
#endif
```

---

## Commit D: Tests

**Goal:** Formal unit tests and `compare_traces.py` script in place.

### New files

**`src/test/unit_tests/search_trace_test.cpp`** — four GoogleTest cases:
1. `HeaderFormat` — verify 5-line header + blank separator
2. `EventOrdering` — solve trivial instance, verify event sequence
3. `MonotonicCounter` — verify counter increments with no gaps
4. `NoOpWhenDisabled` — verify no file created when `open()` not called

**`scripts/compare_traces.py`** — strips 5-line header + blank (`tail -n +7`), then:
- `--full`: exact diff
- `--until-evict`: truncate both traces at first `EVICT` line
- `--until-timeout N`: truncate at operation N
- `--binary` / `--binary-a` / `--binary-b`: invoke binaries with `--trace` to temp files
- Exits 1 on divergence, prints first differing operation number

**`CMakeLists.txt`** — add `search_trace_test.cpp` to `sources_test_unit`; add four
CTest targets gated on `SOLVITAIRE_TRACE`:

| Target | What it checks |
|---|---|
| `trace_identity_flat` | flat binary, same seed twice, exact identity |
| `trace_identity_lru` | lru binary, same seed twice, exact identity |
| `trace_until_eviction` | flat vs LRU, identity until first eviction |
| `trace_until_timeout` | flat binary, timed-out instance, partial identity |

---

## After Commit D: Reference Builds

Once merged to `dev`, build and save trace-enabled release variant binaries:
```bash
cmake -DCMAKE_BUILD_TYPE=Release -DSOLVITAIRE_TRACE=ON -B cmake-build-trace
cmake --build cmake-build-trace --target solvitaire-flat solvitaire-hash-only solvitaire-lru
mkdir -p reference-builds/dev-pre-merge
cp cmake-build-trace/solvitaire-{flat,hash-only,lru} reference-builds/dev-pre-merge/
```

Then merge `dev` into `feature/templated-dispatch`, build again, and run
`compare_traces.py` across a validation suite.
