# Testing Quick-Start

New to the test suite? Start here.

## TL;DR

```bash
# Run all 3 gates (unit tests + level 1 regression) — required before every commit
python3 scripts/run_tests.py

# Fast check after a small change (unit tests only, all 3 configs)
python3 scripts/run_tests.py --quick

# Single gate, binaries already built
python3 scripts/run_tests.py --gate release --skip-build

# See what would run without executing it
python3 scripts/run_tests.py --dry-run
```

## The 3-gate model

All three gates must pass before committing. Each gate targets a different
build configuration and catches different classes of failure.

| Gate | Build config | What it catches |
|------|-------------|-----------------|
| 1 — Release | `cmake-build-release` | Correctness, regression |
| 2 — Trace | `cmake-build-trace` | Search-trace determinism vs reference binary |
| 3 — Debug | `cmake-build-debug` | UB, assertion failures |

`run_tests.py` builds each config automatically before testing it.

## What to run after a code change

- **Touched solver logic (search, move gen, hashing):** all 3 gates — `run_tests.py`
- **Touched trace writer only:** gates 1 and 2 — `run_tests.py --gate release` then `--gate trace`
- **Touched build system / CMakeLists.txt:** all 3 gates
- **Quick sanity check while iterating:** `run_tests.py --quick` (unit tests, ~5 min total)

## GTest vs CTest — what's the difference?

| | GTest (unit_tests binary) | CTest targets |
|---|---|---|
| Run via | `ctest -R ^unit_tests$` or `./cmake-build-*/unit_tests` | `ctest -R <pattern>` |
| What's in it | Unit tests + integration tests + SearchTrace tests | Regression harness, trace identity |
| Compiled with TRACE? | Always yes (even in release/debug build) | Only trace build |

The `unit_tests` binary is **always** compiled with `SOLVITAIRE_SEARCH_TRACE=ON`
so that `SearchTraceTest.*` and `SearchTraceAgreementTest.*` run in all three gates.

Use `ctest -R ^unit_tests$` (anchored regex) — plain `unit_tests` matches other targets.

## CTest targets at a glance

**Release build** (`cmake-build-release`):

| Pattern | Targets matched | Run time |
|---------|----------------|----------|
| `^unit_tests$` | unit_tests | ~2 min |
| `regression_level1` | regression_level1 + flat/hash-only/lru variants | ~2 min |
| `regression_level2` | level 2 + variants (60s/instance) | ~5 min |
| `regression_level3+` | progressively longer | hours |

**Trace build** (`cmake-build-trace`):

| Pattern | Targets matched |
|---------|----------------|
| `^unit_tests$` | unit_tests (includes SearchTrace tests) |
| `trace_` | trace_identity_flat/lru, trace_until_timeout, trace_regression_level1/2 |

`trace_regression_level1/2` require reference binaries in `05-Executables/reference/`.

## Adding a test

See `docs/regression_suite_guide.md` for the full workflow:
- §3: adding Level 1 instances (JSON files + oracle entry)
- §4–5: adding Level 2–5 entries (seed-based)
- §8: trace testing and reference binary system

## Troubleshooting

**`ctest` says "No tests were found"** — you are in the wrong directory. Run from the build dir:
```bash
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
```

**Gate 2 trace regression fails with "ref binary not found"** — set up reference binaries:
```bash
# macOS binary (if you have a known-good build)
cp cmake-build-trace/bin/solvitaire-trace \
   ../../05-Executables/reference/solvitaire-trace-macos
```
See `05-Executables/reference/README.md` for the full reference binary setup.

**Trace tests in release/debug build say "not available"** — expected. The CTest
target `trace_tests_not_available` is disabled in non-trace builds; `run_tests.py`
does not run it.
