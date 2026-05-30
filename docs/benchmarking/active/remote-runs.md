# Running benchmarks on a remote Linux box

How to build and run the benchmark tooling on a remote Linux machine. See
[START-HERE.md](START-HERE.md) for the local quickstart and
[script-inventory.md](script-inventory.md) for what each script does.

## Prerequisites on the remote (one-time)

`scripts/setup_remote.sh` installs cmake / Boost / python3 / R, but make sure the
box also has a C++ compiler and **GNU `parallel`** (the worker engine, required
since Stage 2):

```bash
sudo apt-get update
sudo apt-get install -y build-essential parallel
```

(`setup_remote.sh` now adds `build-essential` and `parallel` to its apt list too,
so a fresh `setup_remote.sh` run will install them; the line above is the manual
equivalent / for boxes set up before that change.)

## Option A — driven from your local machine (recommended)

`scripts/setup_remote.sh` SSHes in, clones/pulls the branch, and builds all four
variant binaries (`solvitaire`, `-flat`, `-hash-only`, `-lru`):

```bash
# First time:
bash scripts/setup_remote.sh \
    --host you@your-linux-box \
    --repo git@github.com:turingfan/ReSolvitaire.git \
    --branch benchmark-rationalisation

# Subsequent (pull latest + rebuild):
bash scripts/setup_remote.sh --host you@your-linux-box --branch benchmark-rationalisation
```

Then SSH in and run (see *The run* below).

## Option B — manually on the box

```bash
ssh you@your-linux-box
git clone git@github.com:turingfan/ReSolvitaire.git
cd ReSolvitaire
git checkout benchmark-rationalisation
./build.sh --release --variants      # ~2 min; builds solvitaire + flat/hash-only/lru
```

## The run

Always preview with `--dry-run` first — it prints the plan **and** the
auto-computed safe worker count and memory budget:

```bash
bash scripts/experiments/bench_multiplicity.sh \
    --phase D --seeds 1-500 --timeout 120000 --dry-run

# then for real (omit --dry-run):
bash scripts/experiments/bench_multiplicity.sh \
    --phase D --seeds 1-500 --timeout 120000
```

### Worker safety (won't crash the box)

You do **not** need to hand-tune `--workers`. It defaults to a memory-aware cap —
`floor(total_RAM × 80% / 3 GB per worker)`, hard-capped at 64 — and drives GNU
`parallel` with `--jobs N --memfree 3G`. If you pass a `--workers` larger than
safe, it **warns and clamps** instead of over-subscribing. Confirm the chosen
number on the `--dry-run` "Workers / Memory budget" line. The same applies to
`benchmark_orchestrator.py` (whose full 14-game matrix additionally requires an
explicit `--full`).

> Background: each worker is a nesting stack (parallel → run_benchmark.py →
> /usr/bin/time → solver), and the flat cache can hold several GB resident, so RAM
> — not core count — is the binding constraint. The old `cpu_count()` default
> could spawn hundreds of processes and exhaust memory; that is fixed.

### Timeouts are clean (no lost work)

When an instance exceeds `--timeout`, the solver self-reports a `timeout` with its
partial search stats; the wrapper records it as `TIMEOUT` (stats intact). The
process-group kill discipline only SIGKILLs a genuinely wedged process (after
SIGTERM + grace), and even then captures any partial output. You should not see
`KILLED`/`TERMINATED` outcomes in normal operation.

## Optional — archive results with provenance

If the `ReSolvitaire-bench` tooling is set up on the box, wrap the run so output
is committed and attributed (see that repo's README for setup):

```bash
bench --detached --bundle-format=tar -m "Stage-2 mult bench" mult-stage2 \
    -- scripts/experiments/bench_multiplicity.sh --phase D --seeds 1-500 --timeout 120000
# scp the bundle back to your local machine, then:  bench-lodge
```
