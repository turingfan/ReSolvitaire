# Next Session Prompt: Search Trace Commit B

## Context

You are continuing implementation of search trace infrastructure on branch
`feature/search-trace`. Read the branch PICKUP first:
`docs/search-trace/PICKUP.md`

Also read `01-Knowledge-Base/AGENTS.md` for mandatory project rules.

## What Was Done Last Session

Commit A is complete (d2b2c09):
- `src/main/solver/search_trace.h` and `search_trace.cpp` — full skeleton
- CMake wiring — `SOLVITAIRE_TRACE` option, ON by default in debug builds
- All `STRACE_*` macros defined; no callsites yet
- Builds clean in release (empty TU) and debug (ENABLED)
- Unit tests pass, regression level 1 4/4

## What To Do Next: Commit B

Add all `STRACE_*` callsites to the solver and cache implementations.
Full details in `docs/search-trace/implementation-plan.md` — Commit B section.

Three files to edit:
1. `src/main/solver/solver.cpp` — MOVE, UNDO, DEPTH, QUERY, HIT, MISS,
   INSERT, LEGAL, RESULT macros at the right points in `dfs()`
2. `src/main/game/generic_flat_cache.h` — EVICT at each `++eviction_count` site
3. `src/main/game/global_cache.cpp` — EVICT at the LRU capacity-eviction site

Before editing, read each file to find the exact line numbers. The approximate
locations in the plan are based on the pre-trace code; verify before inserting.

## Testing Gate for Commit B

```bash
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R ^unit_tests$ --output-on-failure
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
./build.sh --debug --unit-tests
cd cmake-build-debug && ctest -R ^unit_tests$ --output-on-failure
```

**Important:** Use `^unit_tests$` (anchored regex), not `unit_tests` — the
unanchored form also matches `unit_tests_full` which runs for ~5 minutes.

Also manually verify the trace output for a short game:
```bash
./cmake-build-debug/solvitaire --type black-hole --random 1 --trace /tmp/test.trace
head -10 /tmp/test.trace
grep -c "MOVE\|UNDO\|HIT\|MISS" /tmp/test.trace
```

## Constraints

- One commit (Commit B only)
- Stop and report on any test failure — do not debug
- Stop and report on any domain question about event ordering or cache semantics
- The `STRACE_EVICT` placement in `generic_flat_cache.h` must match actual
  eviction sites — verify by reading the file, do not guess from the plan
