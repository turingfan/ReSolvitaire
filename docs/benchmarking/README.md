# Benchmarking Documentation

**New here? Read [active/START-HERE.md](active/START-HERE.md) first** — it tells you
which script to run for which job.

## Structure

```
docs/benchmarking/
├── active/          Current entry point, how-to, contract, and inventory
└── results/         Sample / reference result files (gitignored for generated output)
```

Superseded historical docs (the `mac-dev-benchmark-enhancements` era and earlier
legacy comparison/implementation plans) have been moved out of this repo to
`01-Knowledge-Base/Archive/benchmarking-legacy-docs/`.

## Active Documents

| Document | Purpose |
|---|---|
| [active/START-HERE.md](active/START-HERE.md) | **Start here** — which script for which job, with copy-pasteable commands |
| [active/script-inventory.md](active/script-inventory.md) | Every surviving benchmark script: job, inputs, outputs, status |
| [active/csv_schema.md](active/csv_schema.md) | Authoritative CSV + `solution_type` outcome contract (the bench hook depends on this) |
| [active/quickstart.md](active/quickstart.md) | Worked examples, run your first benchmark |
| [active/design.md](active/design.md) | Architecture, data flow, R integration |
| [active/r_analysis.md](active/r_analysis.md) | R analysis scripts: what they do, how to extend them |

## Status

This area is being rationalised — see the plan at
`01-Knowledge-Base/Implementation-Plans/benchmark-rationalisation-plan-2026-05-29.md`.
Stage 1 (cleanup) is complete; Stage 2 (usability, kill-discipline, worker safety)
is pending. Known rough edges are flagged in START-HERE.
