# Pickup: feature/search-trace

**Last updated:** 2026-05-05  
**Branch:** `feature/search-trace` (from `dev`)

## What This Branch Does

Implements search trace infrastructure for pre-merge validation of
`feature/templated-dispatch`. Instruments the DFS solver to emit a structured
one-line-per-event text log (moves, cache hits/misses/evictions, depth changes).
Two runs can be diffed to confirm identical search behaviour across code changes.

Design document: `docs/proposals/PROPOSAL-search-trace.md` (on `feature/templated-dispatch`;
will be copied to `dev` when this branch merges)

Implementation plan: `docs/search-trace/implementation-plan.md`

## Commits Done

### Commit A: `search_trace` skeleton + CMake wiring (d2b2c09)
- New `src/main/solver/search_trace.h`: `trace_writer` singleton class + all
  `STRACE_*` macros (expand to `((void)0)` when `SOLVITAIRE_SEARCH_TRACE` not defined)
- New `src/main/solver/search_trace.cpp`: `open`/`close`, `write_init`,
  `write_event`/`write_depth`/`write_legal`/`write_move_event`, `write_line`,
  `check_break`, `mtype_str`; all wrapped in `#ifdef SOLVITAIRE_SEARCH_TRACE`
- `CMakeLists.txt`: `search_trace.{cpp,h}` added to `sources_solver`;
  `SOLVITAIRE_TRACE` option (ON by default for Debug, OFF for Release);
  `target_compile_definitions` applied to all targets after their declaration
- Build clean in both release (trace compiles to empty TU) and debug
  (`Search trace: ENABLED` logged at configure time)
- Unit tests pass, regression level 1 4/4

## Commits Remaining

See `docs/search-trace/implementation-plan.md` for full details.

- **Commit B:** `STRACE_*` callsites in `solver.cpp`, `generic_flat_cache.h`, `global_cache.cpp`
- **Commit C:** `--trace <path>` and `--trace-break-at <N>` CLI flags end-to-end
- **Commit D:** Unit tests, `scripts/compare_traces.py`, CTest integration targets

## Testing Gate (each commit)

```bash
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R ^unit_tests$ --output-on-failure
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
```

Debug build gate (Commits B and C):
```bash
./build.sh --debug --unit-tests
cd cmake-build-debug && ctest -R ^unit_tests$ --output-on-failure
```

**Note:** Use `ctest -R ^unit_tests$` (anchored regex), not `ctest -R unit_tests`.
The unanchored form also matches `unit_tests_full` which runs the complete suite
and takes ~5 minutes.

## Key Decisions (from proposal)

- No external library — bespoke ~100-line implementation
- No gzip — plain text; upgrade path documented if ever needed
- Debug builds: tracing ON by default; release builds: OFF by default
- Header: `TRACE v=1`, `DATE`, `CMD` (full argv), `GAME`, `POLICY`, blank line
- Events: `MOVE`, `UNDO`, `DOM`, `DEPTH`, `QUERY`, `HIT`, `MISS`, `INSERT`,
  `EVICT`, `LEGAL`, `SOLVED`/`UNSOLV`/`TIMEOUT`
- flat vs LRU: identical until first true capacity eviction by either cache
