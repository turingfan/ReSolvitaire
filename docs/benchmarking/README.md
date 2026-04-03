# Benchmarking Documentation

## Structure

```
docs/benchmarking/
├── active/          Current design and how-to docs for the benchmark-python branch
├── archive/         Superseded docs from mac-dev-benchmark-enhancements era
└── results/         Sample / reference result files (gitignored for generated output)
```

## Active Documents

| Document | Purpose |
|---|---|
| [active/design.md](active/design.md) | Architecture, data flow, CSV schema, R integration |
| [active/quickstart.md](active/quickstart.md) | Run your first benchmark in 5 minutes |
| [active/csv_schema.md](active/csv_schema.md) | Full column reference for CSV output |
| [active/r_analysis.md](active/r_analysis.md) | R scripts: what they do, how to extend them |

## Archive

The `archive/` folder contains documents from the `mac-dev-benchmark-enhancements`
branch which implemented orchestration in C++ (`--benchmark-seeds` etc.). That
approach has been superseded. The docs are retained for historical reference.
