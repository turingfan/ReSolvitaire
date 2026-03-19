# Regression Suite Guide

This guide describes how to run, maintain, and extend the ReSolvitaire regression suite.

## Suite Structure

The suite is organised into five levels based on the target solve-time of the hardest instance in each set. All oracle values are drawn from the original Solvitaire experimental dataset.

| Level   | Target time | Instances | Oracle                          | Instance files on disk? |
|---------|-------------|-----------|----------------------------------|-------------------------|
| Level 1 | < 1 s       | 150       | `tests/oracles/level1.json`     | Yes — `tests/resources/level1/` |
| Level 2 | ~1 min      | 160       | `tests/oracles/level2.json`     | No (seed-based, see below) |
| Level 3 | ~5 min      | 160       | `tests/oracles/level3.json`     | No (seed-based) |
| Level 4 | ~1 hour     | 160       | `tests/oracles/level4.json`     | No (seed-based) |
| Level 5 | ~6 hours    | 157       | `tests/oracles/level5.json`     | No (seed-based) |

### Why Levels 2–5 have no JSON instance files

The oracle values for levels 2–5 come from seed-based solver runs in the original
experimental dataset. Using `--random <seed>` to reproduce them exactly is important
because exporting a deal to JSON and reloading it produces subtly different node counts
(see `docs/known-issues.md` §1). The JSON instance files for levels 2–5 have therefore
been removed; the runner invokes `--random <seed>` directly.

Level 1 is unaffected — its oracle was generated from JSON files and the runner
continues to pass those files to the solver.

### Memout exclusion policy

Any instance where the original experiment exhausted the transposition-table cache
(`states_removed_from_cache > 0`) is excluded during curation. Such runs may have
missed reachable states and produced incorrect "unsolvable" verdicts. The current
level 5 oracle had four such instances removed:

| Game | Seed | Removed states |
|------|------|---------------|
| free-cell-2-cell | 23 | 145 547 |
| siegecraft | 32 158 | 654 327 |
| spider | 4 026 | 92 322 |
| stronghold | 3 233 | 1 011 |

---

## Running Tests

### Using CTest (recommended for CI)

```bash
# From the build directory:
ctest -R regression_level1          # Level 1 only (~150 runs, < 2 min)
ctest -R regression_level2          # Level 2 only (~160 runs, ~5 min)
ctest -R regression                 # All levels (long)
```

### Using the runner script directly

```bash
python3 scripts/regression_runner.py \
    --exe cmake-build-release/bin/solvitaire \
    --instances tests/resources/level1 \
    --oracle tests/oracles/level1.json \
    --verbose
```

For levels 2–5, the `--instances` directory is not actually used (the runner reads
the oracle for seeds), but a valid path is still required by the argument parser.
Pass any existing directory, e.g. `tests/resources/level1`.

```bash
python3 scripts/regression_runner.py \
    --exe cmake-build-release/bin/solvitaire \
    --instances tests/resources/level1 \
    --oracle tests/oracles/level5.json \
    --max-instance-timeout-ms 21600000   # 6 h cap for level 5
```

### Per-instance timeout cap

The runner imposes a hard cap on how long each instance may run. The default is
**120 000 ms (2 minutes)**, which is appropriate for level 1–3. Raise it for level 4/5:

| Level | Recommended `--max-instance-timeout-ms` |
|-------|----------------------------------------|
| 1–3   | 120 000 (default, 2 min)               |
| 4     | 7 200 000 (2 h)                        |
| 5     | 43 200 000 (12 h)                      |

If the solver's own `--timeout` fires and it exits gracefully, the run counts as
`[WARN/SLOW]` (pass). If the Python watchdog fires (60 s after the solver's deadline),
the run is also counted as `[WARN/SLOW]` rather than `[FAIL]` — a slow machine is not
a correctness failure.

### Interpreting output

| Tag | Meaning |
|-----|---------|
| `[OK]` / progress line | Instance passed (exact outcome + node count match) |
| `[WARN/SLOW]` | Solver timed out but outcome is not contradicted |
| `[FAIL]` | Wrong outcome **or** more nodes than oracle (regression) |
| `[ERROR]` | Runner-level exception (e.g. solver crash, bad JSON) |

A timeout that produces **fewer** nodes than the oracle is not a failure — the machine
is simply slower than the original experiment. A timeout that produces **more** nodes
than the oracle is a failure (the instance should have been pruned earlier).

---

## Regenerating Oracles

### Prerequisites

- The compiled solver binary (`cmake-build-release/bin/solvitaire`)
- The original experimental dataset at a known path (not committed to the repo)

### Step 1 — Curate

```bash
python3 scripts/curate_test_sets.py --set 1m --data-dir /path/to/dataset
python3 scripts/curate_test_sets.py --set 5m --data-dir /path/to/dataset
python3 scripts/curate_test_sets.py --set 1h --data-dir /path/to/dataset
python3 scripts/curate_test_sets.py --set 6h --data-dir /path/to/dataset
```

Each run writes `tests/resources/curated_sets/curated_instances_<set>.json`.

### Step 2 — Export deals and build oracles

```bash
python3 scripts/export_test_deals.py --data-dir /path/to/dataset
```

This reads every curated set, skips memout instances, and writes:
- `tests/oracles/level2.json` … `level5.json`

It does **not** write JSON instance files for levels 2–5 (the runner uses seeds directly).

### Step 3 — Verify

Run the regression suite to confirm all instances pass:

```bash
python3 scripts/regression_runner.py \
    --exe cmake-build-release/bin/solvitaire \
    --instances tests/resources/level1 \
    --oracle tests/oracles/level2.json
```

---

## Maintenance Notes

- **Algorithmic changes** — If the solver's search strategy changes (new pruning,
  different move ordering, etc.), the oracle node counts will drift. Re-run
  `export_test_deals.py` to regenerate from the experimental dataset, or accept
  the new counts by running the solver against each seed and updating the oracle.

- **Custom rules** — Games like `canfield-strict` use a JSON rules file stored in
  `tests/rules/`. The oracle entries carry a `"custom_rules"` key pointing to this
  file; the runner passes `--custom-rules` automatically.

- **Streamliner metadata** — Each oracle entry carries a `"streamliner"` key
  (`"none"`, `"both"`, etc.). The runner always passes `--streamliners <value>` to
  reproduce the exact experiment conditions.

- **Level 1 round-trip caveat** — Level 1 instances are loaded from JSON files. Due
  to the known JSON round-trip bug (see `docs/known-issues.md` §1), a small number of
  level 1 games with reordered tableau piles may show node-count discrepancies if the
  oracle is ever regenerated from seeds rather than from files. This is not currently
  a problem because the level 1 oracle was itself generated from JSON files.
