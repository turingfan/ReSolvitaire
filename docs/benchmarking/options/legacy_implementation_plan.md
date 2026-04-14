# Implementation Plan: Legacy Benchmarking & Handshake Integration

This plan details the steps required to implement the "Option 2" (Semi-Legacy) benchmarking updates from the status document. It is designed to be executed step-by-step by an AI assistant.


## Overview

The goal is to update the Python benchmarking infrastructure to automatically determine the capabilities of a given `solvitaire` binary. Depending on the binary, the orchestrator will:
1. **Modern (`dev`)**: Uses `--json` automatically.
2. **Semi-Legacy**: Uses `--classify` (CSV) but expects the new `[BENCHMARK_CSV_START]` header, memory footprint appends, and graceful parsing.
3. **True Legacy**: Fails safely unless the user passes `--force-legacy`, in which case it uses fragile CSV parsing.  Has an explicit whitelist of arguments/flags which can be provided, otherwise fails.

We will also outline the exact C++ changes needed for the Semi-Legacy branch.

---

## Phase 1: Python Benchmarking Scripts (Branch: `benchmark-python`)

We need to update the Python orchestrator to perform an interrogation "handshake" before spinning up thousands of tasks.

### 1.1. Implement Binary Interrogation in `run_benchmark.py`

Create a function `interrogate_binary(solver_path: str) -> str` that determines the binary type using a unified handshake flag:

- Run `solver_path --benchmark-handshake`.
- **Check 1 (Modern):**
  - If output contains `modern v` (e.g., `"modern v1.0"`), return `"MODERN"`.
- **Check 2 (Semi-Legacy):**
  - If output contains `semi-legacy v` (e.g., `"semi-legacy v0.1"`), return `"SEMI_LEGACY"`.
- **Fallback (True Legacy):**
  - If the solver fails (exits non-zero) because it does not recognize the flag, return `"TRUE_LEGACY"`.

### 1.2. Enforce Handshake Results

In `run_benchmark.py`'s `main()`:
- Call `interrogate_binary(args.solver)`.
- If `"MODERN"`: Proceed with standard JSON execution.
- If `"SEMI_LEGACY"`: Automatically configure the parser to use the updated CSV parser (looking for `[BENCHMARK_CSV_START]`). Pass modern arguments down (because semi-legacy ignores unknowns).
- If `"TRUE_LEGACY"`: 
  - If the user did *not* provide the `--legacy` flag, abort the script with an error: `"True legacy binary detected, but --legacy flag not provided. Aborting for safety."`
  - If `--legacy` *was* provided, proceed with the fragile legacy CSV parser. Ensure that only whitelisted arguments are passed to the solver. **If an argument outside the established True Legacy whitelist (e.g., `--label`) is encountered, the script must FAIL explicitly with an error rather than silently stripping the argument.**

### 1.3. Update the CSV Parsers

Ensure `parse_legacy_classify()` is split or updated:
- **Semi-Legacy Mode:** Scan stdout specifically for `[BENCHMARK_CSV_START]`. Parse the subsequent CSV string. Extract the appended `solver_resident_bytes` at the end of the CSV row.
- **True Legacy Mode:** Retain the current regex-based fragile scanning for 13/24 columns. 

---

## Phase 2: Creating the Semi-Legacy C++ Branch

> [!NOTE]  
> The AI agent executing this should NOT hunt for the specific commit hash. It should ask the user to provide the starting point or assume a baseline has been provided.

### 1. Branch Strategy

1. Checkout the historical commit that represents the "pure" Solvitaire engine (prior to modern refactoring). Example: `git checkout <historical-v1-commit>`.
2. Create a new branch: `git checkout -b legacy/semi-legacy-benchmark`.

### 2. C++ Source Modification (Surgical Backports)

The agent must make the following minimal changes to the C++ code on this new branch:

#### A. Graceful Argument Ignoring & Handshake
In `src/main/input-output/input/command_line_helper.cpp`:
- Find the Boost Program Options instantiation (`po::store(po::parse_command_line(...)`).
- Change it to allow unregistered options so modern flags (`--label`, `--cache-capacity`) don't crash it:
  `po::command_line_parser(argc, argv).options(desc).allow_unregistered().run()`
- Add a tiny check at the very top of `main()` or the command line helper:
  ```cpp
  if (argc > 1 && std::string(argv[1]) == "--benchmark-handshake") {
      std::cout << "semi-legacy v0.1\n";
      return 0;
  }
  ```
*(Note: A parallel update must be made in the `dev` branch so the modern code returns `"modern v1.0"` to the same flag.)*

#### B. Reliable CSV Headers
In the file where `--classify` output is generated (likely `main.cpp` or `solvability_calc.cpp` where the 13/24 column CSV is printed):
- Prepend the strict marker right before the CSV data dumps:
  ```cpp
  std::cout << "[BENCHMARK_CSV_START]\n";
  ```

### 3. Internal Memory Tracking
In the same location where the CSV is finalized:
- Backport the internal memory footprint gathering code (`get_peak_rss()` or similar equivalents). This must explicitly be ported across OSes if possible (e.g., porting the Windows `GetProcessMemoryInfo` blocks alongside macOS/Linux) to maximize comparability.
- After printing the final depth / final solution type, append the byte count of the resident memory:
  ```cpp
  std::cout << ", " << get_peak_rss_or_equivalent();
  ```

### 3. Verification

Once compiled, test the binary against the new `run_benchmark.py`:
```bash
# It should automatically detect SEMI_LEGACY, not crash, block out noise, and capture memory.
python3 scripts/run_benchmark.py --solver path/to/semi/solvitaire --instances tests/resources/benchmark/klondike/*.json --cache-capacity 100000 
```


