# Running Multiplicity Benchmarks on a Remote Server

Quick-start for Stage 5.4 benchmarks on a fresh Linux server.

All experiments go through the `bench` wrapper so results are archived in DataLad.
The remote server doesn't need DataLad — `bench --detached` produces bundles that
you lodge locally afterwards.

---

## 1. Server Setup

```bash
# Essentials
sudo apt-get update
sudo apt-get install -y build-essential cmake git python3 jq \
    libboost-program-options-dev
```

## 2. Install bench + Clone Code

From your **local Mac** (SSH agent forwarding required):

```bash
ssh -A user@server 'bash -s' < \
    ~/Research/ReSolvitaire-project/02-Code-Repositories/ReSolvitaire-bench/bootstrap/setup-remote.sh \
    -- --code-repo git@github.com:<your-user>/ReSolvitaire.git
```

This clones `ReSolvitaire-bench` to `~/ReSolvitaire-bench`, sets up `bench` in PATH,
creates `~/bench-bundles/`, and optionally clones the code repo.

If the bootstrap doesn't work or isn't appropriate, manual setup:

```bash
# On the remote
git clone <bench-repo-url> ~/ReSolvitaire-bench
echo 'export PATH="$HOME/ReSolvitaire-bench/bin:$PATH"' >> ~/.bashrc
echo 'export BENCH_BUNDLE_DIR="$HOME/bench-bundles"' >> ~/.bashrc
mkdir -p ~/bench-bundles
source ~/.bashrc

git clone <code-repo-url> ~/ReSolvitaire
```

## 3. Build

```bash
cd ~/ReSolvitaire
git checkout multiplicity-encoding

# Release build (all variant binaries)
./build.sh --release

# From-scratch variant for Comparison A
cmake --build cmake-build-release --target solvitaire-mult-scratch

# Sanity check
./cmake-build-release/bin/solvitaire --type klondike --random 1 --json
```

Verify these binaries exist in `cmake-build-release/bin/`:
- `solvitaire` (multiplicity via `--cache-type multiplicity`)
- `solvitaire-flat`
- `solvitaire-lru`
- `solvitaire-mult-scratch` (Comparison A only)

## 4. Smoke Test (without bench)

Quick sanity check that the benchmark script works:

```bash
./scripts/experiments/bench_multiplicity.sh \
    --phase D --seeds 1-5 --games klondike --workers 1

ls benchmarks/mult_*/combined_D_*.csv
```

## 5. Quick Validation via bench (Phase 1)

50 seeds, all 4 comparisons. Uses `bench --detached` so everything is archived.

```bash
bench --detached --bundle-format tar \
    -m "Multiplicity Phase 1: 50 seeds, all comparisons" \
    mult-phase1 \
    -- scripts/experiments/bench_multiplicity.sh \
       --phase ABCD --seeds 1-50 --workers 8 \
       --outdir '$BENCH_RUN_DIR/data'
```

The `--outdir '$BENCH_RUN_DIR/data'` puts CSVs inside the bench run directory so
they're included in the bundle. The single quotes are important — `$BENCH_RUN_DIR`
is expanded by bench at runtime.

Check Comparison A agreement (states_searched must be identical):
```bash
OUTDIR=$(ls -td benchmarks/mult_* | head -1)
paste <(cut -d, -f5 "$OUTDIR/combined_A_incr.csv") \
      <(cut -d, -f5 "$OUTDIR/combined_A_scratch.csv") | \
    awk -F'\t' 'NR>1 && $1 != $2 {print "MISMATCH line " NR ": " $1 " vs " $2; err++} END {if(!err) print "All match"}'
```

Review Phase 1 results before proceeding.

## 6. Full Run via bench (Phase 2)

500 seeds, all comparisons. Use tmux so it survives disconnection.

```bash
tmux new -s bench

bench --detached --bundle-format tar \
    -m "Multiplicity Phase 2: 500 seeds, all comparisons" \
    mult-phase2 \
    -- scripts/experiments/bench_multiplicity.sh \
       --phase ABCD --seeds 1-500 --workers 16 \
       --outdir '$BENCH_RUN_DIR/data'
```

Estimated time: ~2-3 hours with 16 workers.

## 7. Retrieve and Lodge Results

On your **local Mac**:

```bash
# Copy bundles from remote
scp "user@server:~/bench-bundles/*.tar.gz" ~/bench-bundles/

# Lodge into DataLad results repo
cd ~/Research/ReSolvitaire-project/04-Results
bench-lodge --batch ~/bench-bundles/
```

`bench-lodge` unpacks each bundle, moves it into `04-Results/runs/`, appends to
`manifest.jsonl`, commits with DataLad, and pushes metadata + annexed content.

## 8. Analysis (local)

```bash
cd ~/Research/ReSolvitaire-project/04-Results
RUNDIR=$(ls -td runs/*mult-phase2* | head -1)

# Quick summary
Rscript ../02-Code-Repositories/claude-Resolvitaire/analysis/summary.R \
    "$RUNDIR/data/combined_D_mult.csv"

# Full comparison report (HTML)
Rscript ../02-Code-Repositories/claude-Resolvitaire/analysis/benchmark.R \
    --baseline "$RUNDIR/data/combined_D_lru.csv" \
    --current  "$RUNDIR/data/combined_D_mult.csv" \
    --output   "$RUNDIR/data/report_D.html"
```

## What Success Looks Like

| Comparison | Pass criterion |
|---|---|
| A: Incremental vs scratch | Identical `states_searched`; incremental >= 2x faster |
| B: Flat vs multiplicity | Flat <= 30% faster (acceptable overhead) |
| C: LRU vs multiplicity | Multiplicity >= 2x faster |
| D: LRU vs mult+symmetry | **Multiplicity >= 1.5x faster, comparable solve rate** |

## Dry Run

Preview all commands without running anything:
```bash
./scripts/experiments/bench_multiplicity.sh \
    --dry-run --phase ABCD --seeds 1-500 --workers 16
```

## Troubleshooting

- **Missing binary**: `Phase A skipped` means `solvitaire-mult-scratch` wasn't built. Run `cmake --build cmake-build-release --target solvitaire-mult-scratch`
- **Out of memory**: The flat cache mmaps 3.2 GB virtual. Ensure the server has >= 4 GB RAM or reduce `--cache-capacity` via solver args
- **Stuck processes**: `Ctrl-C` in the terminal kills all workers cleanly (process group signal)
- **bench preflight failure**: Code tree must be clean (all committed). Use `BENCH_FORCE_DIRTY=1` as escape hatch if needed
- **$BENCH_RUN_DIR not expanding**: Make sure `--outdir` uses single quotes so bench expands it, not your shell

## Reference

- Bench tool docs: `02-Code-Repositories/ReSolvitaire-bench/README.md`
- Bench design rationale: `02-Code-Repositories/ReSolvitaire-bench/DESIGN.md`
- Results repo: `04-Results/README.md`
- Benchmark plan: `docs/multiplicity-encoding/stage5-4-benchmark-plan.md`
