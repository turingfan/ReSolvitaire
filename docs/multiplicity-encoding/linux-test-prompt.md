# Prompt for Claude Code on the Web — Linux Test

**Copy everything below the line into Claude Code on the Web.**

---

Run all 3 test gates on the `multiplicity-encoding` branch to verify recent changes (Scheme A fix + KI-21 merge) pass on Linux. This is test-only — do not modify source code.

Read `CLAUDE.md` for build/test commands, then run gates **sequentially** (not in parallel):

```bash
git checkout multiplicity-encoding && git pull

# Gate 1
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R ^unit_tests$ --output-on-failure
ctest -R regression_level1 --output-on-failure
cd ..

# Gate 2
./build.sh --trace
cd cmake-build-trace && ctest -R ^unit_tests$ --output-on-failure
ctest -R "trace_identity|trace_until_timeout|trace_mult_vs_flat" --output-on-failure
cd ..

# Gate 3
./build.sh --debug --unit-tests
cd cmake-build-debug && ctest -R ^unit_tests$ --output-on-failure
```

Do NOT run `trace_regression_level1/2` (missing reference binaries — known issue #22).

Report which gates passed/failed and any error output.
