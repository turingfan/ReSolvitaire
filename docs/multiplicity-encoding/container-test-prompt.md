# Prompt for Claude Code on the Web — Linux Tests for Scheme A Fix

**Copy everything below the line into Claude Code on the Web as the initial prompt.**

---

## Task

Run the Linux build and tests on the `multiplicity-encoding` branch to verify the Scheme A collapsing bug fix. This is a **test-only** task — do not modify any source code.

## Setup

```bash
git checkout multiplicity-encoding
git pull origin multiplicity-encoding
```

Verify the latest commits include one with message starting "WIP: fix Scheme A collapsing bug".

## Before You Run Tests

Read `CLAUDE.md` in the repo root — specifically the build commands and testing sections.

## Test Execution

Run these commands **sequentially** (never in parallel):

### Step 1: Gate 1 — Release

```bash
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R ^unit_tests$ --output-on-failure
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
```

### Step 2: Gate 2 — Trace

```bash
./build.sh --trace
cd cmake-build-trace && ctest -R ^unit_tests$ --output-on-failure
cd cmake-build-trace && ctest -R "trace_identity|trace_until_timeout" --output-on-failure
```

### Step 3: Gate 3 — Debug

```bash
./build.sh --debug --unit-tests
cd cmake-build-debug && ctest -R ^unit_tests$ --output-on-failure
```

### Step 4: Report results

After all steps, report:
- Which gates passed/failed
- Any test output showing failures (copy exact error lines)
- Whether `SchemeACollapsingPredecessorRegression` appears in the unit test output

## Rules

- **Do NOT modify any source files.** This is test-only.
- **Run gates sequentially**, one at a time.
- If a gate fails, still run the remaining gates and report all results.
- Do NOT run trace regression tests (`trace_regression_level1/2`) — they require reference binaries not present.

## What Success Looks Like

All three gates pass:
- Gate 1: unit_tests PASS, regression_level1 (all variants) PASS
- Gate 2: unit_tests PASS, trace_identity PASS, trace_until_timeout PASS
- Gate 3: unit_tests PASS (debug build exercises `verify_against_scratch()` on every move)
