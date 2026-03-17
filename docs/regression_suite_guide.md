# Regression Suite Guide

This guide provides instructions for maintaining and using the ReSolvitaire regression suite, which spans multiple levels of search complexity.

## Suite Structure

The regression suite is organized into levels based on target search times per instance:

| Level   | Target Time | Instances | Location                          | Oracle                          |
|---------|-------------|-----------|-----------------------------------|---------------------------------|
| Level 1 | Rapid       | ~20       | `tests/resources/level1`          | `tests/oracles/level1.json`     |
| Level 2 | 1 minute    | 160       | `tests/resources/level2`          | `tests/oracles/level2.json`     |
| Level 3 | 5 minutes   | 160       | `tests/resources/level3`          | `tests/oracles/level3.json`     |
| Level 4 | 1 hour      | 160       | `tests/resources/level4`          | `tests/oracles/level4.json`     |
| Level 5 | 6 hours     | 161       | `tests/resources/level5`          | `tests/oracles/level5.json`     |

## Running Tests

### Using CTest (Recommended)
From your build directory:
```bash
ctest -R regression_level1  # Run Level 1
ctest -R regression         # Run all regression levels
```

### Using the Runner Script
For more control or verbose output:
```bash
python3 scripts/regression_runner.py --exe <path_to_solvitaire> --instances tests/resources/level2 --oracle tests/oracles/level2.json --verbose
```

## Creating/Adding Instances

### 1. Curation
Use `scripts/curate_test_sets.py` to identify interesting instances from large experimental datasets.
```bash
python3 scripts/curate_test_sets.py --set 1m --data-dir /path/to/dataset
```
This generates `curated_instances_1m.json` with relative paths to the source CSVs.

### 2. Export
Use `scripts/export_test_deals.py` to generate the individual JSON deal files and the baseline oracle.
```bash
python3 scripts/export_test_deals.py --data-dir /path/to/dataset
```
This will:
1. Read the curated JSONs.
2. Generate JSON deals in `tests/resources/levelX/`.
3. Generate/Update oracles in `tests/oracles/levelX.json`.

## Maintenance Notes
- **Deterministic Oracles**: Oracles are generated based on historical performance. If the solver's algorithmic behavior changes, oracles may need regeneration.
- **Relative Paths**: All scripts support a `--data-dir` argument to avoid hardcoded absolute paths to the large experimental datasets.
- **Custom Rules**: Instances for games like `accordion` and `gaps` automatically use custom rule files located in `tests/rules/`.
