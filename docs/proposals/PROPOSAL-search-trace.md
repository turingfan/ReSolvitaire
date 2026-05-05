# Proposal: Search Trace Infrastructure

**Date:** 2026-05-04  
**Updated:** 2026-05-05  
**Status:** Approved — implementation plan at `docs/search-trace/implementation-plan.md`
**Tracked in:** `docs/known-issues.md` §18

---

## Purpose

The primary use case is **pre-merge validation of `feature/templated-dispatch`**: run the
same game instance under `dev` and the new branch, diff the traces, and confirm that the
DFS traversal is byte-for-byte identical. Secondary uses: debugging, performance analysis,
regression diagnosis.

The old `dual_cache` metamorphic harness (deleted in Commit 5) compared caches within a
single run. This system compares two independent runs, which is more general and doesn't
require simultaneous access to both code paths.

---

## Goals

1. Zero overhead when disabled — compile-time guard eliminates all trace code from release
   binaries.
2. Simple text format — one line per event; `diff` or `comm` are sufficient to compare two
   runs.
3. Consistent across policies — move notation from `move.h`, never hashes (hashing can
   legitimately differ between policies).
4. Operation numbering with break-and-inspect — re-run and print the game state at a given
   operation number.
5. One-liner call sites — macro-based; all concrete formatting/IO in separate files.
6. Test suite covering: full identity, identity until timeout, identity until first eviction.

---

## Non-Goals

- Performance profiling (timing per operation) — the trace is for correctness, not perf.
- Hash logging — hashes can legitimately differ between policies; move sequences are the
  invariant.
- Multi-threaded tracing (the solver is single-threaded).
- gzip compression at creation — see note below.

---

## Note on gzip

An earlier draft required gzip output. After investigating C++ logging libraries and
considering the actual use case, gzip was dropped:

- **Merge validation runs are one-shot** — generate, diff, delete. No archiving.
- **The diff workflow is simpler without compression** — plain `diff a.txt b.txt` rather
  than `zcat a.gz | tail -n +5 > a.txt && ...`.
- **Trace size is manageable for validation-scale runs** — the intended comparison runs are
  short seed-based instances, not 60-second timeout runs. At that scale, plain text files
  are fine.
- **Adding gzip later is a self-contained change** — it would touch only
  `search_trace.cpp` (swap `FILE*` for `gzFile`, add zlib dependency). The format,
  macros, and all callsites are unaffected. If trace files prove unwieldy in practice, the
  upgrade is straightforward using either the `zlib` gzFile API directly or the
  single-header `gzip-hpp` wrapper from Mapbox.

---

## Library vs Custom Implementation

Investigated spdlog, loguru, Quill, NanoLog, Boost.Log, and log4cplus. Conclusion:
**a small bespoke implementation is the right choice.** The reasons:

- No library natively provides a monotonic operation counter or our exact fixed-field line
  format. Every library would require a custom formatter and counter wrapper — roughly the
  same effort as writing directly.
- Our use case is narrow: single-threaded, one output file, ~12 event types, plain text.
  General-purpose logging libraries bring threading infrastructure, log-level hierarchies,
  and sink management that we don't need.
- Total implementation is approximately 80–100 lines in `search_trace.cpp`.

**Patterns adopted from library research:**

- **`#ifdef SOLVITAIRE_SEARCH_TRACE` for compile-time elimination** — macros expand to
  `((void)0)` when the guard is not defined. Same approach as spdlog's
  `SPDLOG_ACTIVE_LEVEL`. Compiler completely removes call sites and their arguments.
- **Plain `bool` runtime gate** — single-threaded, no atomic needed. Branch is perfectly
  predicted after the first call.
- **Lazy evaluation is safe here** — macro arguments are simple values (a `move` struct, a
  depth `uint64_t`, a count `size_t`). No expensive expressions at callsites, so argument
  evaluation when disabled is not a concern. Worth a comment in the header for future
  maintainers.
- **Stack-buffer formatting** — `snprintf` into a local `char[256]` per event, then
  `fwrite`. No heap allocation per event.

---

## Build Default: On in Debug, Off in Release

Tracing is primarily a debugging and validation tool. The CMake option defaults:

- **Debug builds** (`./build.sh --debug`): `SOLVITAIRE_SEARCH_TRACE` defined by default,
  including the regular `solvitaire` binary and all variant targets.
- **Release builds** (`./build.sh` or `./build.sh --release`): `SOLVITAIRE_SEARCH_TRACE`
  not defined by default. Can be enabled explicitly with `-DSOLVITAIRE_TRACE=ON` at
  configure time.

This means tracing is available whenever a developer is running a debug build, without
any extra flags — and is completely absent from production release binaries.

---

## Compile-Time Guard

```cmake
option(SOLVITAIRE_TRACE "Enable search trace instrumentation"
       $<IF:$<CONFIG:Debug>,ON,OFF>)
if(SOLVITAIRE_TRACE)
    foreach(tgt solvitaire solvitaire-flat solvitaire-hash-only solvitaire-lru)
        target_compile_definitions(${tgt} PRIVATE SOLVITAIRE_SEARCH_TRACE)
    endforeach()
endif()
```

---

## New Files

| File | Purpose |
|---|---|
| `src/main/solver/search_trace.h` | Public API: macros + `trace_writer` class declaration |
| `src/main/solver/search_trace.cpp` | Implementation: plain file write, line formatting, singleton |
| `src/test/unit_tests/search_trace_test.cpp` | Unit tests for trace output and comparison |
| `scripts/compare_traces.py` | Header-stripping diff helper for integration tests |

Callsites in `solver.cpp` add one macro call per event. No other files need significant
changes.

---

## Runtime Flags

Two new CLI options (only meaningful when `SOLVITAIRE_SEARCH_TRACE` defined; silently
ignored otherwise with a stderr warning):

```
--trace <path>            Write trace to <path> (plain text).
                          Enables tracing for this run.

--trace-break-at <N>      Re-run the solve; when the global operation counter
                          reaches N, print the full game state to stdout and exit.
                          Requires --trace to identify the target run.
```

---

## Trace Format

### Header

```
TRACE v=1
DATE 2026-05-05T14:32:01
CMD ./solvitaire --type klondike --random 42 --trace run.trace
GAME type=klondike seed=42 streamliner=none
POLICY flat

```

Five lines plus a blank separator before the event stream. The DATE field uses ISO 8601
local time. The CMD field is the full reconstructed argv, which makes it easy to re-run
the exact solve. The header is stripped before diffing (`tail -n +6`).

`POLICY` is one of `flat`, `hash-only`, `predecessor`, `lru` — set from the dispatch
branch in `main.cpp`.

### Event Lines

Every event line begins with a zero-padded 10-digit operation counter, then a keyword,
then key=value fields. All fields are always present in fixed order (missing fields use
`-`). This makes lines fixed-width enough that standard tools work cleanly.

```
0000000001 DEPTH d=1
0000000002 MOVE  t=regular f=13 to=0 c=1 rev=0 flip=0 dom=0
0000000003 QUERY
0000000004 MISS
0000000005 INSERT
0000000006 LEGAL n=7
0000000007 MOVE  t=regular f=0 to=2 c=1 rev=0 flip=0 dom=0
0000000008 QUERY
0000000009 HIT
0000000010 DEPTH d=0
0000000011 UNDO  t=regular f=13 to=0 c=1 rev=0 flip=0 dom=0
0000000012 EVICT
0000000013 DOM   t=regular f=5 to=foundations c=1 rev=0 flip=0 dom=1
0000000014 UNDO  t=regular f=5 to=foundations c=1 rev=0 flip=0 dom=1
0000000015 SOLVED
```

**Event types:**

| Keyword | Meaning | Fields |
|---|---|---|
| `DEPTH` | Search depth changed (after make_move or undo_move) | `d=<n>` |
| `MOVE` | `make_move` called | move fields (see below) |
| `UNDO` | `undo_move` called | move fields |
| `DOM` | Dominance move made (also fires a `MOVE` event) | move fields |
| `QUERY` | Cache lookup started (contains / insert) | none |
| `HIT` | Cache reported state already seen | none |
| `MISS` | Cache reported state is new | none |
| `INSERT` | State inserted into cache | none |
| `EVICT` | A slot was evicted to make room | none |
| `LEGAL` | Number of legal moves generated at this node | `n=<count>` |
| `SOLVED` | Solver found solution | none |
| `UNSOLV` | Solver exhausted search space | none |
| `TIMEOUT` | Solver timed out | none |

**Notes on event ordering at a node:**

```
MOVE   ← make_move
DEPTH  ← depth incremented
QUERY  ← insert_t / insert_with_iterator
MISS   ← is_new_state = true
INSERT ← state added to cache
LEGAL  ← get_legal_moves result
  [recurse into children]
UNDO   ← undo_move
DEPTH  ← depth decremented
```

When `HIT` fires (is_new_state = false), `INSERT` and `LEGAL` do not fire.

**Move fields** (always in this order):

```
t=<mtype>  f=<pile_ref>  to=<pile_ref>  c=<count>  rev=<0|1>  flip=<0|1>  dom=<0|1>
```

`mtype` values: `regular`, `built_group`, `stock_k_plus`, `stock_to_all_tableau`,
`sequence`, `accordion`, `null`.

Pile refs are logged as integers (the `pile::ref` value). These are stable structural
indices independent of cache policy, so they diff cleanly across runs.

---

## One-Liner Callsites

When `SOLVITAIRE_SEARCH_TRACE` is not defined, all macros expand to `((void)0)` and the
compiler removes all callsites and their arguments entirely.

```cpp
// search_trace.h — disabled path
#define STRACE_MOVE(mv)    ((void)0)
#define STRACE_UNDO(mv)    ((void)0)
#define STRACE_DOM(mv)     ((void)0)
#define STRACE_DEPTH(d)    ((void)0)
#define STRACE_QUERY()     ((void)0)
#define STRACE_HIT()       ((void)0)
#define STRACE_MISS()      ((void)0)
#define STRACE_INSERT()    ((void)0)
#define STRACE_EVICT()     ((void)0)
#define STRACE_LEGAL(n)    ((void)0)
#define STRACE_RESULT(r)   ((void)0)
#define STRACE_INIT(type, seed, streamliner, policy)  ((void)0)
```

When defined, each macro calls into the singleton with a runtime enable check:

```cpp
#define STRACE_MOVE(mv)  trace_writer::instance().write_move(mv)
// etc.
```

Note: macro arguments are all simple values (`move` struct, integral types). No expensive
expressions appear at callsites, so argument evaluation in the disabled path is not a
practical concern — but a comment in the header documents this assumption for future
maintainers.

Callsites in `solver.cpp` (approximate locations):

```cpp
// make_move path (line ~179):
state.make_move(current_node->mv);
STRACE_MOVE(current_node->mv);
res.depth++;
STRACE_DEPTH(res.depth);

// cache insert path (line ~137):
STRACE_QUERY();
is_new_state = cache.insert_t(state);
if (is_new_state) { STRACE_MISS(); STRACE_INSERT(); }
else              { STRACE_HIT(); }

// legal moves (line ~152):
vector<move> next_moves = state.get_legal_moves(current_node->mv);
STRACE_LEGAL(next_moves.size());

// undo_move path (line ~215):
state.undo_move(current_node->mv);
STRACE_UNDO(current_node->mv);
res.depth--;
STRACE_DEPTH(res.depth);
```

Eviction events fire inside `generic_flat_cache.h` at the `++eviction_count` sites and
inside `lru_cache` at the eviction path. These are the only callsites outside `solver.cpp`.

---

## trace_writer Implementation Sketch

```cpp
class trace_writer {
    FILE*    file_    = nullptr;
    uint64_t op_      = 0;      // plain uint64_t — single-threaded, no atomic needed
    bool     enabled_ = false;
    uint64_t break_at_ = UINT64_MAX;

public:
    static trace_writer& instance();   // Meyer's singleton

    void open(const std::string& path, int argc, char** argv);
    void close();                      // called from destructor

    bool enabled() const { return enabled_; }
    void set_break_at(uint64_t n) { break_at_ = n; }

    void write_move(const move& mv);
    void write_undo(const move& mv);
    void write_event(const char* keyword);
    void write_depth(uint64_t d);
    void write_legal(size_t n);
    void write_init(const std::string& game_type, int seed,
                    const std::string& streamliner, const std::string& policy);

private:
    void write_line(const char* fmt, ...);  // snprintf into char[256], then fwrite
};
```

The `break_at_` check happens inside `write_line`: after incrementing `op_`, if
`op_ == break_at_`, the solver is interrupted and the game state printed. The game state
access requires the solver to pass `state` through to the trace call at that point —
the implementation plan details how this is wired.

---

## Operation Counter and Break-at-N

Every `write_line` increments `op_` before writing. The counter value appears on the line.

`--trace-break-at N` re-runs the same solve with tracing enabled to the same `--trace`
path (overwriting it). When `op_` reaches N, the current game state is printed to stdout
and the process exits cleanly. This lets a developer take the first-differing operation
number from a `diff` output and immediately inspect the state at that point.

---

## Cross-Policy Comparison Semantics

Cache hits and misses are **identical between flat and LRU** up to the first true capacity
eviction by either cache. Before any eviction, both caches retain all inserted states and
therefore agree on every HIT/MISS decision. After an eviction, the evicted state may be
re-inserted as a false new state, causing the traces to diverge — this is expected and
correct. The `--until-evict` integration test therefore compares flat vs LRU (not just
flat vs flat) and asserts identity up to the first `EVICT` line in either trace.

---

## Reference Builds for Merge Validation

Before `feature/templated-dispatch` is merged, reference binaries must be built from
`dev` with tracing enabled. The workflow:

1. Implement search trace on `dev` (this work).
2. Build and save the `dev` reference trace-enabled binaries
   (`solvitaire-flat`, `solvitaire-hash-only`, `solvitaire-lru`).
3. Merge `dev` (with tracing) into `feature/templated-dispatch`.
4. Build the same three trace-enabled binaries from the merged branch.
5. Run `compare_traces.py` across a validation suite (several seeds, multiple game types).
6. Zero divergence → merge is safe.

The reference binaries are not committed to the repo. The procedure for building them is
documented in `docs/search-trace/merge-validation-procedure.md` (written when tracing
lands on `dev`).

---

## Testing Plan

### Unit tests (GoogleTest, within `unit_tests` binary)

1. **Header format** — `STRACE_INIT` produces the correct 5-line header + blank separator,
   including DATE and CMD fields.
2. **Event ordering** — solve a trivial 1-card instance; verify events appear in the
   correct sequence (MOVE, DEPTH, QUERY, MISS, INSERT, LEGAL, ..., UNDO, DEPTH).
3. **Operation counter** — verify counter is strictly monotonically increasing with no gaps.
4. **No-op when disabled** — when runtime flag is off, no file is created, counter stays 0.

### Integration tests (script-driven, CTest targets)

These require a trace-enabled build (`-DSOLVITAIRE_TRACE=ON` or debug build).
`scripts/compare_traces.py` handles header stripping and diff:

```bash
# Exact full identity (same binary, same seed — must be identical)
compare_traces.py --binary ./solvitaire-flat --game klondike --seed 42

# Identity until first eviction (flat vs LRU — identical until capacity eviction)
compare_traces.py --until-evict \
    --binary-a ./solvitaire-flat --binary-b ./solvitaire-lru \
    --game freecell --seed 1

# Identity until timeout
compare_traces.py --until-timeout 5000 \
    --binary-a ./solvitaire-flat --binary-b ./solvitaire-flat \
    --game klondike --seed 99

# Cross-branch comparison (primary merge validation use case)
compare_traces.py --binary-a ./dev-build/solvitaire-flat \
                  --binary-b ./new-build/solvitaire-flat \
                  --game klondike --seed 42
```

The script exits non-zero on divergence and prints the first differing operation number.

**CTest targets:**

| Target | What it checks |
|---|---|
| `trace_identity_flat` | flat binary, same seed run twice, exact identity |
| `trace_identity_lru` | lru binary, same seed run twice, exact identity |
| `trace_until_eviction` | flat vs LRU, identity until first eviction |
| `trace_until_timeout` | flat binary, timed-out instance, partial identity |

---

## Additional Notes

### Why a singleton, not a solver member

`solver_impl<Policy>` is templated; passing a `trace_writer&` through it adds noise to
every template instantiation. Since the solver is single-threaded and there is only ever
one active trace per run, a global singleton with a runtime enable gate is simpler and
keeps callsites as one-liners without any context threading.

### LRU eviction semantics

The LRU cache's `set_non_live` call marks a state as backtracked-from but the entry stays
in cache. True eviction happens when the Boost MultiIndex list overflows capacity. The
trace logs `EVICT` only on actual capacity evictions, not `set_non_live`.

### Trace size (plain text)

Klondike, 60-second timeout: roughly 10–50 million DFS nodes. At ~8 events per node and
~35 bytes per line, that is 3–14 GB. For merge validation, runs should be chosen to be
tractable (short seeds, not full timeout runs) to keep traces in the tens-of-MB range.
If archiving or large-run comparison is ever needed, gzip can be added as a self-contained
change to `search_trace.cpp`.
