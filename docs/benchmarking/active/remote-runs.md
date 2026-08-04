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

## Option C — Apptainer / Singularity (SLURM / HPC clusters)

On clusters that provide `apptainer` (or `singularity`) instead of
container/docker/podman, build a self-contained image from `solvitaire.def` (the
companion to the Dockerfile) and run the benchmark **inside** it — binaries, GNU
`parallel`, and libs are all baked in.

### The sturm runbook (whole process verified end-to-end 2026-08-04)

The complete working process on the sturm cluster, as actually run — not
speculation. Repo lives at `~/ReSolvitaire-sturm/ReSolvitaire`; the cluster is
x86_64. Apptainer **auto-binds `$HOME`**, so the container sees the same `~/...`
paths as the host — no explicit `--bind` needed for anything under home.

**Ground rules (all verified the hard way):**

- Every job submission needs an explicit partition **and** QOS: `-p sturm -q sturm`
  (`salloc`, `srun`, and `#SBATCH` alike).
- `salloc` only **grants** an allocation — your shell stays on the login node. Get a
  compute-node shell with `srun -p sturm -q sturm -c 16 --mem=64G -t 4:00:00 --pty bash`
  (or `salloc` first, then a bare `srun --pty bash` inside it).
- **Do ALL builds and runs on a compute node.** `apptainer build` (parallel make +
  LTO) crashed/overloaded the login node when tried there. Runs must be in-job
  anyway: the bench worker sizing reads the job cgroup's memory limit.

**1. Build the image** (compute node, ~minutes):

```bash
cd ~/ReSolvitaire-sturm/ReSolvitaire
git fetch && git checkout dev && git pull
apptainer build --fakeroot solvitaire.sif solvitaire.def
```

**2. Regression gates in-container** (verified green):

```bash
CONTAINER_RUNTIME=apptainer ./scripts/container-build.sh --editable --regression
```

**3. Benchmark run** — dry-run first, always:

```bash
mkdir -p "$PWD/benchout"
apptainer exec --bind "$PWD/benchout":/out solvitaire.sif bash -c \
  'cd /workspace && scripts/experiments/bench_multiplicity.sh \
     --phase D --games klondike --seeds 1-50 --timeout 120000 \
     --outdir /out/run1 --dry-run'
```

Check the dry-run's **Workers / Memory budget** line reports the *allocation's*
memory (verified: cgroup-aware sizing works under SLURM). Then rerun without
`--dry-run`; results land in `benchout/run1/` on the host, with clean
`TIMEOUT`s and no `KILLED` rows.

**4. Linux trace reference / trace regression** (KI-27 closure, verified):
build the reference from the re-baseline commit inside the container, then point
the trace build at it. `~` paths work because home is auto-bound.

```bash
git worktree add ~/resolv-ref-7eb5883 7eb5883
apptainer exec solvitaire.sif bash -c 'cd ~/resolv-ref-7eb5883 && ./build.sh --trace'
mkdir -p ~/05-Executables/reference     # = $REPO_ROOT/../../05-Executables/reference
cp ~/resolv-ref-7eb5883/cmake-build-trace/bin/solvitaire-trace \
   ~/05-Executables/reference/solvitaire-trace-reference-linux-amd64-20260603-7eb5883

apptainer exec solvitaire.sif bash -c 'cd ~/ReSolvitaire-sturm/ReSolvitaire && ./build.sh --trace'
apptainer exec solvitaire.sif bash -c \
  'cd ~/ReSolvitaire-sturm/ReSolvitaire && cmake -DTRACE_REF_BIN=$HOME/05-Executables/reference/solvitaire-trace-reference-linux-amd64-20260603-7eb5883 cmake-build-trace'
apptainer exec solvitaire.sif bash -c \
  'cd ~/ReSolvitaire-sturm/ReSolvitaire/cmake-build-trace && ctest -R "^trace_regression_level1$" --output-on-failure'
```

Result 2026-08-04: `trace_regression_level1` **Passed, 35.39 s** (mac took ~36 s).
With the reference in `~/05-Executables/reference/`,
`./scripts/container-build.sh --editable --trace-regression` also finds it by
its default path (the script arch-detects amd64 vs arm64 since 2026-08-04).
Keep a copy of the amd64 reference binary in the project's
`05-Executables/reference/` on the mac too (scp it back) so both arches live
together.

```bash
ssh you@cluster
git clone git@github.com:turingfan/ReSolvitaire.git
cd ReSolvitaire && git checkout benchmark-rationalisation
apptainer build --fakeroot solvitaire.sif solvitaire.def    # ~2-4 min (parallel build)
```

Run the bench script in-container, writing results to a **host-bound** dir (the image
filesystem is read-only, so output must NOT go to `/workspace`):

```bash
mkdir -p "$PWD/benchout"
apptainer exec --bind "$PWD/benchout":/out solvitaire.sif bash -c \
  'cd /workspace && scripts/experiments/bench_multiplicity.sh \
     --phase D --games klondike --seeds 1-500 --timeout 120000 \
     --outdir /out/run1 --dry-run'        # drop --dry-run for the real run
```

Results land in `./benchout/run1/` on the host. The dry-run / worker-safety / clean-timeout
behaviour is identical to Options A/B (see **The run** below) — only the wrapping differs.
Apptainer-specific notes:

- **`scripts/container-build.sh` now supports apptainer** (detects apptainer/singularity;
  override with `CONTAINER_RUNTIME=apptainer`). It builds the `.sif` from `solvitaire.def`
  and runs the `--test/--regression/--variants/--trace-*` flags via `apptainer exec`. Add
  **`--editable`** to build the image once and bind the live host repo at `/workspace`, so
  script edits need no rebuild and C++ edits only an incremental compile into the host-bound
  `cmake-build-*` dirs (dev/benchmarking — not production). The manual `apptainer build`/`exec`
  above still works if you prefer driving it yourself. *Caveat:* `--editable` shares the host
  `cmake-build-*` dirs with the container, so don't mix host-native and in-container builds of
  different arch in the same checkout.
- **Limits are the SLURM allocation, not the node.** apptainer runs under the job's
  cgroup, and the worker sizing is cgroup-aware — it detects the *allocation's* memory
  limit (confirm on the dry-run's detected-limit line). Memory usually caps workers below
  your core count at the default cache.
- **Phase-D LRU memory caveat:** auto-sizing budgets per worker by the multiplicity reserve
  (~6.5 GiB) and assumes LRU stays under it, but the LRU variant has been seen peaking
  ~11 GB on klondike. On a tight allocation, cap `--workers ≈ allocation_GB × 0.8 / 11`
  (e.g. ~30 on 480 G) to avoid a SLURM OOM during the LRU phase. See KI-28.
- **`--fakeroot`** is for unprivileged builds (drop it if building as root). `%files .
  /workspace` copies `.git` + host build dirs — build from a clean checkout if it's slow.
- **Don't run `ctest`/test gates inside the read-only SIF** without `--writable-tmpfs`;
  plain benchmark runs write nothing to the image, so they're fine.

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
