# Relative Benchmarking Framework Details

**Objective:** Implement a strictly deterministic, platform-agnostic microsecond timing harness entirely contained within C++, and orchestrated by an external Python script. The solution must provide raw numbers, stats (mean/median/sd/min/max), the comparative Speedup Ratio (`Current Time / Baseline Time`), and **Standard Candle Normalization** to account for absolute hardware differences. Finally, it must log all execution environment metadata (Date, Git Hash, Machine ID).

## Proposed Changes

### Phase 1: Update Command-Line Parsing
**Target Files:**
- `[MODIFY] src/main/input-output/input/command_line_helper.h`
- `[MODIFY] src/main/input-output/input/command_line_helper.cpp`

**Implementation Details:**
1. **Header Updates:** In `class command_line_helper`, add new private fields and public getters:
   ```cpp
   std::pair<int, int> benchmark_seeds = {0, 0};
   int benchmark_iterations = 1;
   bool benchmark_warmup = true;
   bool is_benchmark = false;

   std::pair<int, int> get_benchmark_seeds() const;
   int get_benchmark_iterations() const;
   bool get_benchmark_warmup() const;
   bool get_is_benchmark() const;
   ```
2. **CPP Updates:** Inside `command_line_helper::parse_command_line(...)`, bind new flags to Boost `program_options`:
   - `--benchmark-seeds`: Set `is_benchmark = true` and store the bounds in `benchmark_seeds`.
   - `--benchmark-iterations`: Integer flag defaulting to `1`.
   - `--benchmark-warmup`: Boolean flag defaulting to `true`.
3. Validate: If `is_benchmark` is true, ensure `--json` is conceptually enforced.

---

### Phase 2: Overhaul C++ Benchmark Engine
**Target Files:**
- `[MODIFY] src/main/evaluation/benchmark.h`
- `[MODIFY] src/main/evaluation/benchmark.cpp`

**Implementation Details:**
1. **Signature:** Change `static void benchmark::run(...)` to accept the seed bounds, iterations, and a warmup boolean flag.
2. **Timing Loop Structure:**
   - Create a top-level JSON `Document` using rapidjson.
   - For `current_seed` = `seeds.first` up to `seeds.second`:
     - If `warmup` is true, instantiate `solver sol_warmup(gs, cache_capacity)` and execute `sol_warmup.run()` without timing it. This brings the CPU to full frequency and fills caches, neutralizing initial hardware latency.
     - Run a secondary loop `for(int i = 0; i < iterations; ++i):`
     - Inside inner loop, instantiate the `solver sol(gs, cache_capacity)`.
     - `auto start = std::chrono::steady_clock::now();`
     - `solver::result result = sol.run();`
     - `auto end = std::chrono::steady_clock::now();`
     - Store the `duration_cast<microseconds>(end - start).count()` in an internal `std::vector<double> inner_times`.
   - After computing all seeds, aggregate all valid timings to an `all_times` vector.
3. **Statistical Math Formulas:** Use `<numeric>` and `<algorithm>`:
   - **Mean:** `std::accumulate(all_times.begin(), all_times.end(), 0.0) / all_times.size()`
   - **Median:** Sort `all_times` and extract the middle element.
   - **SD (Standard Deviation):** Compute variance `accumulate( (x - mean)^2 ) / size`, output `sqrt(variance)`.
   - **Min/Max:** `*std::min_element()` and `*std::max_element()`.
4. **JSON Serialization:** Use RapidJSON to emit a purely structured block containing raw arrays and the `aggregate_stats` object out to `std::cout`, suppressing normal text output.

---

### Phase 3: Hook the Engine Execution
**Target File:**
- `[MODIFY] src/main/main.cpp`

**Implementation Details:**
1. After parsing the command line, check `helper.get_is_benchmark()`.
2. Call `benchmark::run` with the extracted parameters.
3. Exit immediately (`return 0;`). **Do not allow the code to reach the standard interactive game loop.**

---

### Phase 4: Create the Python Orchestrator & Standard Candle
**Target File:**
- `[NEW] scripts/compare_benchmarks.py`

**Implementation Details:**
1. **Libraries:** `argparse`, `subprocess`, `json`, `math`, `datetime`, `platform`, `os`.
2. **Core Functionality - Standard Candle:** 
   - Accept `--candle-exe` (defaults to baseline Solvitaire). The script must execute this executable on a hardcoded "simple workload" (e.g., Klondike seeds 1 to 5, 5 iterations) and measure the `standard_candle_mean_us`. This serves as the absolute hardware benchmark. 
   - All subsequent timing runs can be normalized: `normalized_sys_score = benchmark_mean_us / standard_candle_mean_us`. This handles discrepancies where a run on a 2019 Intel Mac appears "slower" than a Linux server.
3. **Execution Logic:**
   - Execute the baseline binary and capture its C++ JSON payload.
   - Execute the current binary and capture its C++ JSON payload.
4. **Metadata Enrichment:**
   - The Python script must dynamically inject environment metadata into the final combined JSON log:
     - `date`: `datetime.datetime.now().isoformat()`
     - `machine_id`: `platform.node()`
     - `git_hash`: `subprocess.check_output(['git', 'rev-parse', 'HEAD']).decode().strip()`
     - `standard_candle_us`: The calculated candle metric.
5. **Calculations & Display:**
   - Print a visible markdown-style terminal report.
   - Highlight the **Speedup Ratio**: `Current_Mean / Baseline_Mean`.
   - Display absolute timings alongside the **Hardware Normalized Score**.
   - Output the massive enriched JSON dictionary to a hard file `benchmark_report.json` for CI tracking.

## Verification Plan

### Automated Check
- Modify the `CMakeLists.txt` `make test` environment to assert that `./solvitaire --type klondike --benchmark-seeds 1 2 --benchmark-iterations 1` outputs parseable JSON.

### Manual Integration Evaluation
- Once completed, the AI coder should add an arbitrary `std::this_thread::sleep_for(std::chrono::microseconds(100));` inside `src/main/game/move.cpp`.
- The python orchestrator `compare_benchmarks.py` must detect the exact regression ratio, while accurately printing the Git Hash, Date, and Standard Candle metric.
