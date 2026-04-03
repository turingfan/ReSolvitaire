# Evaluation Checklist: `benchmark-python` Branch

**Reviewer:** Opus/Sonnet
**After:** Haiku completes haiku-implementation-brief.md

For each item: verify directly (read file / run command / check output).
Do not trust Haiku's self-report — check independently.

---

## 1. Branch and cherry-pick

- [ ] Branch `benchmark-python` exists and is checked out
- [ ] `git log --oneline` shows the cherry-pick commit for `extract_benchmark_results.py`
      (commit `00147b78`)
- [ ] `scripts/extract_benchmark_results.py` exists and is unchanged from `mac-dev`
- [ ] No spurious extra commits (e.g. no merge commits from unrelated branches)

---

## 2. C++ — `--json` output

Run:
```bash
./cmake-build-release/bin/solvitaire --type klondike --random 1 --json 2>/dev/null
```

- [ ] Output is valid JSON (pipe through `python3 -m json.tool`)
- [ ] Contains `solution_type` (value is one of: `winnable`, `unsolvable`, `timeout`)
- [ ] Contains `states_searched` (integer)
- [ ] Contains `unique_states` (integer)
- [ ] Contains `backtracks` (integer)
- [ ] Contains `max_depth` (integer)
- [ ] Contains `dominance_moves` (integer) — **NEW**
- [ ] Contains `states_removed_from_cache` (integer) — **NEW**
- [ ] Contains `cache_size` (integer) — **NEW**
- [ ] Contains `cache_buckets` (integer) — **NEW**
- [ ] Contains `final_depth` (integer) — **NEW**
- [ ] Contains `solver_resident_bytes` (integer > 0) — **NEW**
- [ ] Build still passes: `./build.sh --release` exits 0
- [ ] Unit tests pass: `cd cmake-build-release && ctest -R '^unit_tests$' --output-on-failure`
- [ ] No new CLI flags in `command_line_helper` (check `--help` output is unchanged)

---

## 3. Python script — existence and interface

- [ ] `scripts/run_benchmark.py` exists
- [ ] File is executable (`ls -l scripts/run_benchmark.py` shows `x` bit)
- [ ] `python3 scripts/run_benchmark.py --help` exits 0 and shows expected flags:
      `--solver`, `--seeds`, `--instances`, `--type`, `--output`,
      `--iterations`, `--warmup`, `--timeout`, `--streamliner`,
      `--cache-capacity`, `--output-json`, `--no-header`, `--no-summary`

---

## 4. Python script — CSV output correctness

Run:
```bash
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-5 --timeout 10000 \
    --output /tmp/test_bench.csv --no-summary
```

- [ ] Exits 0
- [ ] `/tmp/test_bench.csv` exists
- [ ] First row is a header row (starts with `instance,`)
- [ ] Has exactly 6 rows total (1 header + 5 data)
- [ ] Columns present (check header): `instance, seed, run, solution_type,
      time_us, nodes, unique_nodes, backtracks, dominance_moves,
      states_removed_from_cache, cache_size, cache_buckets, max_depth,
      final_depth, resident_memory_bytes, virtual_memory_bytes,
      solver_resident_bytes, streamliner, cache_capacity, timeout_ms,
      solver_commit`
- [ ] `instance` column values are `klondike_1` … `klondike_5`
- [ ] `seed` column values are integers 1–5
- [ ] `run` column value is `1` for all rows (single iteration default)
- [ ] `solution_type` values are one of: `SOLVED`, `UNWINNABLE`, `TIMEOUT`
      (not `winnable` / `unsolvable` — mapping must be applied)
- [ ] `time_us` values are positive floats
- [ ] `nodes` values are positive integers
- [ ] `resident_memory_bytes` values are positive integers (> 1MB = 1048576)
- [ ] `solver_commit` is a 7-char hex string

---

## 5. Python script — warmup excluded

Run:
```bash
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-3 --timeout 10000 \
    --warmup 1 --iterations 2 \
    --output /tmp/test_warmup.csv --no-summary
```

- [ ] `/tmp/test_warmup.csv` has 7 rows (1 header + 6 data: 3 seeds × 2 iterations)
- [ ] `run` column values are `1` and `2` (not `0` for warmup)

---

## 6. Python script — `--no-header`

Run:
```bash
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-2 --timeout 10000 \
    --output /tmp/test_noheader.csv --no-summary --no-header
```

- [ ] `/tmp/test_noheader.csv` has exactly 2 rows (no header)
- [ ] First row starts with `klondike_` (data, not column names)

---

## 7. Python script — JSON output

Run:
```bash
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-3 --timeout 10000 \
    --output /tmp/test_bench2.csv \
    --output-json /tmp/test_bench2.json --no-summary
python3 -m json.tool /tmp/test_bench2.json > /dev/null
```

- [ ] `/tmp/test_bench2.json` is valid JSON (command above exits 0)
- [ ] JSON is an array of 3 objects
- [ ] Each object has `instance`, `solution_type`, `time_us` keys

---

## 8. Python script — instance mode

Run:
```bash
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --instances tests/resources/level1/klondike/*.json \
    --timeout 10000 \
    --output /tmp/test_instances.csv --no-summary
```

- [ ] Exits 0
- [ ] CSV has more than 1 data row
- [ ] `seed` column is empty/blank for all rows
- [ ] `instance` column contains basenames (e.g. `klondike_001`)

---

## 9. Python script — automatic R summary

Run:
```bash
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-5 --timeout 10000 \
    --output /tmp/test_summary.csv
```
(Note: no `--no-summary`)

- [ ] If R is available: output to stdout includes `=== Benchmark Summary:`
- [ ] If R is not available: script does not crash; prints a message about R not being found
- [ ] Either way, `/tmp/test_summary.csv` is correctly written

---

## 10. Python script — graceful error handling

Run with a bad solver path:
```bash
python3 scripts/run_benchmark.py \
    --solver /nonexistent/solvitaire \
    --type klondike --seeds 1-2 --timeout 5000 \
    --output /tmp/test_error.csv --no-summary
```

- [ ] Script does not crash with an uncaught Python exception
- [ ] Either exits with a helpful error message, or writes ERROR rows to CSV

---

## 11. R scripts — existence

- [ ] `analysis/functions.R` exists
- [ ] `analysis/summary.R` exists
- [ ] `analysis/benchmark.R` exists

---

## 12. R scripts — `summary.R`

```bash
Rscript analysis/summary.R /tmp/test_bench.csv
```

- [ ] Exits 0 (or exits with a clear error if R packages missing)
- [ ] Output contains `=== Benchmark Summary:`
- [ ] Output contains `Instances:`, `Solved:`, `Timing`, `Nodes`
- [ ] Output contains `Geometric mean:` and `PAR2 score:`
- [ ] Output contains `Memory` section if `resident_memory_bytes` is non-zero

---

## 13. R scripts — `benchmark.R`

```bash
# Create two test CSVs
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-20 --timeout 10000 \
    --output /tmp/baseline.csv --no-summary
cp /tmp/baseline.csv /tmp/current.csv

Rscript analysis/benchmark.R \
    --baseline /tmp/baseline.csv \
    --current /tmp/current.csv \
    --output /tmp/comparison.html
```

- [ ] Exits 0
- [ ] `/tmp/comparison.html` exists and is non-empty (> 500 bytes)
- [ ] HTML contains `Benchmark Comparison Report`
- [ ] HTML contains `Geometric mean speedup`
- [ ] HTML contains `Wilcoxon`
- [ ] Scatter plot PNG is created alongside the HTML

---

## 14. R scripts — JSON input

```bash
Rscript analysis/summary.R /tmp/test_bench2.json
```

- [ ] Exits 0 (or clear package error)
- [ ] Output contains `=== Benchmark Summary:`

---

## 15. Housekeeping

- [ ] `results/` is in `.gitignore` (check: `grep 'results/' .gitignore`)
- [ ] `results/.gitkeep` exists (or `results/` directory is otherwise tracked)
- [ ] `scripts/compare_benchmarks.py` has deprecation comment near top
- [ ] `CLAUDE.md` contains a `## Benchmarking` section with `run_benchmark.py` example

---

## 16. Scope — nothing extra changed

- [ ] `git diff dev..benchmark-python -- src/main/game/` shows no changes
      (except possibly `benchmark.cpp` if memory stats needed adding there)
- [ ] `git diff dev..benchmark-python -- CMakeLists.txt` is empty
- [ ] `git diff dev..benchmark-python -- .github/` is empty
- [ ] No new C++ files added

---

## 17. Final integration test

```bash
# 20-seed end-to-end run with summary
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-20 --timeout 30000 \
    --output /tmp/final_test.csv

# Verify row count
wc -l /tmp/final_test.csv
# Expected: 21 (header + 20 data rows)
```

- [ ] Row count is 21
- [ ] No blank lines in the middle of the file
- [ ] All `solution_type` values are valid
- [ ] `time_us` values are all > 0
- [ ] `solver_commit` is the same value in all rows
