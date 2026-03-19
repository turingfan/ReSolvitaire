# Regression Suite Guide

This guide describes how to run, maintain, and extend the ReSolvitaire regression suite.

## Suite Structure

The suite is organised into five levels based on the target solve-time of the hardest instance in each set. All oracle values are drawn from the original Solvitaire experimental dataset and include the exact streamliner settings used in that experiment.

| Level   | Curation set | Target time | Instances | Oracle                      | Instance files on disk? |
|---------|-------------|-------------|-----------|------------------------------|-------------------------|
| Level 1 | —           | < 1 s       | 150       | `tests/oracles/level1.json` | Yes — `tests/resources/level1/` |
| Level 2 | `1m`        | ~1 min      | 160       | `tests/oracles/level2.json` | No (seed-based) |
| Level 3 | `5m`        | ~5 min      | 160       | `tests/oracles/level3.json` | No (seed-based) |
| Level 4 | `1h`        | ~1 hour     | 160       | `tests/oracles/level4.json` | No (seed-based) |
| Level 5 | `6h`        | ~6 hours    | 160       | `tests/oracles/level5.json` | No (seed-based) |

### Why Levels 2–5 have no JSON instance files

The oracle values for levels 2–5 come from seed-based solver runs in the original
experimental dataset. Using `--random <seed>` to reproduce them exactly is important
because exporting a deal to JSON and reloading it can produce subtly different node counts
(see `docs/known-issues.md` §1). The runner invokes `--random <seed>` directly.

Level 1 is different — its oracle was generated from JSON instance files and the runner
continues to pass those files to the solver. The streamliner used for each Level 1
instance matches the ground-truth experiment (46 instances use `--streamliners both`,
104 use `--streamliners none`).

### Memout exclusion policy

Any instance where the original experiment exhausted the transposition-table cache
(`states_removed_from_cache > 0`) is excluded during curation. Such runs may have
missed reachable states and produced incorrect "unsolvable" verdicts. The memout filter
is applied at curation time in `curate_test_sets.py`, so excluded instances can never
appear in an oracle.

### Instances missing from some game types

Spider and Accordion have no "unwinnable" instances in the experimental dataset at
any difficulty level — their data contains only solvable deals. All other game types
have both winnable and unwinnable instances in every level oracle.

---

## Running Tests

### Using CTest (recommended for CI)

```bash
# From the build directory:
ctest -R regression_level1          # Level 1 only (~150 runs, < 2 min)
ctest -R regression_level2          # Level 2 only (~160 runs, ~5 min)
ctest -R regression                 # All levels (long)
ctest                               # All tests including unit_tests
```

### Using the runner script directly

```bash
python3 scripts/regression_runner.py \
    --exe cmake-build-release/bin/solvitaire \
    --instances tests/resources/level1 \
    --oracle tests/oracles/level1.json \
    --verbose
```

For levels 2–5, `--instances` is not used (the runner reads seeds from the oracle),
but the argument is still accepted. Pass any existing directory:

```bash
python3 scripts/regression_runner.py \
    --exe cmake-build-release/bin/solvitaire \
    --instances tests/resources/level1 \
    --oracle tests/oracles/level5.json \
    --max-instance-timeout-ms 43200000   # 12 h cap for level 5
```

### Per-instance timeout cap

The runner imposes a hard cap on how long each instance may run. The default is
**120 000 ms (2 minutes)**, appropriate for levels 1–3. Raise it for levels 4–5:

| Level | Recommended `--max-instance-timeout-ms` |
|-------|----------------------------------------|
| 1–3   | 120 000 (default, 2 min)               |
| 4     | 7 200 000 (2 h)                        |
| 5     | 43 200 000 (12 h)                      |

If the solver's own `--timeout` fires and exits gracefully, the run counts as
`[WARN/SLOW]` (pass). If the Python watchdog fires (60 s after the solver's deadline),
the run is also `[WARN/SLOW]` — a slow machine is not a correctness failure.

### Interpreting output

| Tag | Meaning |
|-----|---------|
| `[OK]` / progress line | Instance passed (exact outcome + node count match) |
| `[WARN/SLOW]` | Solver timed out but outcome is not contradicted |
| `[FAIL]` | Wrong outcome **or** more nodes than oracle (regression) |
| `[ERROR]` | Runner-level exception (e.g. solver crash, bad JSON) |

A timeout that produces **fewer** nodes than the oracle is not a failure — the machine
is simply slower than the original experiment. A timeout that produces **more** nodes
is a failure (the instance should have been pruned earlier).

---

## Regenerating Oracles

### Prerequisites

- The compiled solver binary (`cmake-build-release/bin/solvitaire`)
- The original experimental dataset at a known path (not committed to the repo)

### Levels 2–5: single command

Oracle generation is integrated into curation. Running `curate_test_sets.py` writes
both the curated-instances metadata and the oracle in one step:

```bash
python3 scripts/curate_test_sets.py --set 1m --data-dir /path/to/dataset
python3 scripts/curate_test_sets.py --set 5m --data-dir /path/to/dataset
python3 scripts/curate_test_sets.py --set 1h --data-dir /path/to/dataset
python3 scripts/curate_test_sets.py --set 6h --data-dir /path/to/dataset
```

Each run writes:
- `tests/resources/curated_sets/curated_instances_<set>.json` — full curation metadata
- `tests/oracles/level{2,3,4,5}.json` — the regression oracle

### Level 1: generate_baseline.py

Level 1 uses pre-selected JSON instance files. Regenerate the oracle with:

```bash
python3 scripts/generate_baseline.py \
    --exe cmake-build-release/bin/solvitaire \
    --instances tests/resources/level1 \
    --output tests/oracles/level1.json \
    --data-dir /path/to/dataset
```

The `--data-dir` argument looks up per-instance streamliner settings from the
experimental dataset. Without it, all instances run with `--streamliners none`, which
will produce different node counts for the 46 smart-run winnable instances.

### Verify after regeneration

```bash
python3 scripts/regression_runner.py \
    --exe cmake-build-release/bin/solvitaire \
    --instances tests/resources/level1 \
    --oracle tests/oracles/level1.json

python3 scripts/regression_runner.py \
    --exe cmake-build-release/bin/solvitaire \
    --instances tests/resources/level1 \
    --oracle tests/oracles/level2.json
```

---

## Maintenance Notes

- **Algorithmic changes** — If the solver's search strategy changes (new pruning,
  different move ordering, etc.), oracle node counts will drift. Re-run
  `curate_test_sets.py` (levels 2–5) or `generate_baseline.py` (level 1) to regenerate
  from the experimental dataset, or update the oracle by running the solver against each
  seed/instance and recording the new counts.

- **Custom rules** — Games like `canfield-strict` use a JSON rules file stored in
  `tests/rules/`. The oracle entries carry a `"custom_rules"` key; the runner passes
  `--custom-rules` automatically.

- **Streamliner metadata** — Each oracle entry carries a `"streamliner"` key
  (`"none"`, `"both"`, etc.). The runner always passes `--streamliners <value>` to
  reproduce the exact experiment conditions.

- **Ground-truth dataset** — The dataset is at
  `/Users/ipg/Research/ReSolvitaire-project/03-Large-Datasets/solvitaire-paper-v10-Feb2026`
  (local only, not committed). All oracle values are traceable back to it.
