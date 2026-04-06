# Known Issues during "Hash-Only Cache" Benchmarking

## Benchmarking Script Limitations

1. **Passthrough Arguments in `run_benchmark.py`**:
   The current testing framework does not elegantly pass arbitrary solver flags to the underlying `solvitaire` invocation run during benchmarking orchestrations (except for a hardcoded whitelist like `--streamliners` and `--cache-capacity`). Testing bespoke parameters like `--cache-type hash-only` required manually generating a shell script wrapper for the binary and substituting it in as the `--solver` parameter.

2. **Game Type Ambiguity**:
   Certain variants such as Baker's Dozen do not have direct 1:1 translations to the commonly colloquialized name under the `--type` flag. Running the tests across differing variants required looking through the source code in `sol_preset_types.cpp` manually. The solver defaults for `-test-...` parameters are not thoroughly exposed.

3. **`solvitaire --help` Incompleteness**:
   The `solvitaire --help` summary provides extensive flag documentation but leaves retrieving specific game variants to an alternative path using `--available-game-types`. Better linking between the two screens could expedite benchmarking and testing setup workflows.
