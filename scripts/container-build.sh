#!/bin/bash
# Build and optionally run tests inside a Linux container.
# Usage: ./scripts/container-build.sh [--test] [--regression] [--editable] ...
#
# Supports two runtime families:
#   * OCI:       the 'container' CLI, Docker, or Podman  (build an image, run -m)
#   * Apptainer: apptainer / singularity                 (build a .sif, exec --bind)
# Auto-detected; override with CONTAINER_RUNTIME=container|docker|podman|apptainer|singularity.
#
# --editable (dev/benchmarking, NOT production): instead of baking the source+binaries
# into the image on every change, build the image ONCE (toolchain) and bind the LIVE
# host repo into the container at run time. Edits to scripts take effect with no rebuild;
# edits to C++ need only an incremental compile into the host-bound cmake-build-* dirs.
#
# Memory note (OCI only): the flat_cache mmaps a large virtual address space (default
# 100M entries ≈ 6.4 GB virtual). OCI runtimes enforce a vmem limit, so test runs use
# -m 7g. Apptainer does not impose this; it inherits the host/cgroup (SLURM) limit.
#
# BuildKit cache note (OCI 'container' CLI): --no-cache is the default because the CLI
# does not reliably invalidate the COPY layer on source change. If stale sources persist
# despite --no-cache, run:  container builder delete --force

set -u

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE_NAME="solvitaire-dev"          # OCI image tag
SIF_NAME="${SIF_NAME:-solvitaire.sif}"   # apptainer image file (in repo root)
DEF_FILE="solvitaire.def"

TEST_FLAG=""
REGRESSION_FLAG=""
VARIANTS_FLAG=""
TRACE_TEST_FLAG=""
TRACE_REGRESSION_FLAG=""
EXTRACT_TRACE_BINARY_FLAG=""
EDITABLE_FLAG=""

# Linux reference trace binary (KI-27 re-baseline 2026-06-03). Host path + the path it
# is mounted/copied to inside the container for --trace-regression.
# Arch-dependent: Apple-container VMs on Apple Silicon are arm64; real Linux hosts
# (e.g. the sturm cluster) are typically x86_64 -> amd64 (verified 2026-08-04).
case "$(uname -m)" in
    x86_64|amd64)  _ref_arch="amd64" ;;
    aarch64|arm64) _ref_arch="arm64" ;;
    *)             _ref_arch="$(uname -m)" ;;
esac
REF_BIN_NAME="solvitaire-trace-reference-linux-${_ref_arch}-20260603-7eb5883"
LINUX_REF_BIN_HOST="${REPO_ROOT}/../../05-Executables/reference/${REF_BIN_NAME}"
LINUX_REF_BIN_CONTAINER="/05-Executables/reference/${REF_BIN_NAME}"

NO_CACHE_FLAG="--no-cache"           # OCI only; --use-cache clears it
MEMORY_LIMIT="7g"                    # OCI only
# apptainer build opts (override APPTAINER_BUILD_OPTS="" if you build as root)
APPTAINER_BUILD_OPTS="${APPTAINER_BUILD_OPTS:---fakeroot}"

print_usage() {
    cat << EOF
Usage: ./scripts/container-build.sh [OPTIONS]

Options:
  --test                 Run unit tests after build (cmake-build-release)
  --regression           Run regression_level1 tests after build (main binary only)
  --variants             Run regression_level1 tests for all four variant binaries
                         (solvitaire-flat, solvitaire-hash-only, solvitaire-lru, solvitaire-bitmap)
  --trace-test           Run unit_tests + trace_identity + trace_until_timeout from cmake-build-trace
  --trace-regression     Run trace_regression_level1 using the Linux reference binary
                         from 05-Executables/reference/ (run --extract-trace-binary first to create it)
  --extract-trace-binary Copy solvitaire-trace binary out of the image to ./solvitaire-trace-linux-amd64
  --editable             Build the image ONCE (toolchain) and bind the live host repo at
                         run time; build binaries into host cmake-build-* dirs. No image
                         rebuild on edits. For dev/benchmarking — not production.
  --use-cache            (OCI) allow cached layers (faster, but may use stale sources)
  (no options)           Build the image only

Runtime: auto-detected (container/docker/podman → OCI; apptainer/singularity → apptainer).
Override with CONTAINER_RUNTIME=<name>. Apptainer build opts via APPTAINER_BUILD_OPTS
(default --fakeroot; set empty to build as root).

Examples:
  ./scripts/container-build.sh --test                  # build + unit tests
  ./scripts/container-build.sh --editable --test       # build once, bind live repo, test
  CONTAINER_RUNTIME=apptainer ./scripts/container-build.sh --editable --regression
EOF
}

# ─── Parse arguments ─────────────────────────────────────────────────────────
for arg in "$@"; do
    case "$arg" in
        --test)                 TEST_FLAG="1" ;;
        --regression)           REGRESSION_FLAG="1" ;;
        --variants)             VARIANTS_FLAG="1" ;;
        --trace-test)           TRACE_TEST_FLAG="1" ;;
        --trace-regression)     TRACE_REGRESSION_FLAG="1" ;;
        --extract-trace-binary) EXTRACT_TRACE_BINARY_FLAG="1" ;;
        --editable)             EDITABLE_FLAG="1" ;;
        --use-cache)            NO_CACHE_FLAG="" ;;
        --help|-h)              print_usage; exit 0 ;;
        *)
            echo "Unknown argument: $arg"; print_usage; exit 1 ;;
    esac
done

# ─── Detect runtime ──────────────────────────────────────────────────────────
# RUNTIME_KIND is "oci" or "apptainer"; CONTAINER_CMD is the binary.
CONTAINER_CMD=""
RUNTIME_KIND=""
_try() { command -v "$1" &> /dev/null; }
if [ -n "${CONTAINER_RUNTIME:-}" ]; then
    CONTAINER_CMD="$CONTAINER_RUNTIME"
    case "$CONTAINER_RUNTIME" in
        apptainer|singularity) RUNTIME_KIND="apptainer" ;;
        *)                     RUNTIME_KIND="oci" ;;
    esac
    _try "$CONTAINER_CMD" || { echo "Error: CONTAINER_RUNTIME='$CONTAINER_CMD' not found on PATH."; exit 1; }
elif _try container; then CONTAINER_CMD="container"; RUNTIME_KIND="oci"
elif _try docker;    then CONTAINER_CMD="docker";    RUNTIME_KIND="oci"
elif _try podman;    then CONTAINER_CMD="podman";    RUNTIME_KIND="oci"
elif _try apptainer; then CONTAINER_CMD="apptainer"; RUNTIME_KIND="apptainer"
elif _try singularity; then CONTAINER_CMD="singularity"; RUNTIME_KIND="apptainer"
else
    echo "Error: no container runtime found (container/docker/podman/apptainer/singularity)."
    exit 1
fi
echo "Using runtime: $CONTAINER_CMD ($RUNTIME_KIND)"
[ -n "$EDITABLE_FLAG" ] && echo "Editable mode: live host repo bound at /workspace (no rebuild on edits)."

cd "$REPO_ROOT"

# ─── Build the image ─────────────────────────────────────────────────────────
# In --editable mode the image is just a toolchain that we reuse, so build it only if
# it is missing. Otherwise (re)build so baked source/binaries are current.
build_image() {
    if [ "$RUNTIME_KIND" = "apptainer" ]; then
        if [ -n "$EDITABLE_FLAG" ] && [ -f "$SIF_NAME" ]; then
            echo "Reusing existing $SIF_NAME (editable mode)."; return 0
        fi
        echo "Building $SIF_NAME from $DEF_FILE ..."
        $CONTAINER_CMD build $APPTAINER_BUILD_OPTS "$SIF_NAME" "$DEF_FILE"
    else
        if [ -n "$EDITABLE_FLAG" ] && $CONTAINER_CMD image inspect "$IMAGE_NAME" &> /dev/null; then
            echo "Reusing existing image '$IMAGE_NAME' (editable mode)."; return 0
        fi
        echo "Building image '$IMAGE_NAME' ..."
        $CONTAINER_CMD build $NO_CACHE_FLAG --build-arg CACHEBUST="$(date +%s)" -t "$IMAGE_NAME" .
    fi
}

# run_in "<bash command>" [extra bind/-v spec]
# Runs the command inside the container. In editable mode the live repo is bound at
# /workspace so the command sees current source + writes build outputs back to the host.
run_in() {
    local cmd="$1"; local extra="${2:-}"
    if [ "$RUNTIME_KIND" = "apptainer" ]; then
        local binds=""
        [ -n "$EDITABLE_FLAG" ] && binds="--bind ${REPO_ROOT}:/workspace"
        [ -n "$extra" ] && binds="$binds $extra"
        # shellcheck disable=SC2086
        $CONTAINER_CMD exec $binds "$SIF_NAME" bash -c "cd /workspace && $cmd"
    else
        local binds=""
        [ -n "$EDITABLE_FLAG" ] && binds="-v ${REPO_ROOT}:/workspace"
        [ -n "$extra" ] && binds="$binds $extra"
        # shellcheck disable=SC2086
        $CONTAINER_CMD run --rm -m "$MEMORY_LIMIT" $binds "$IMAGE_NAME" bash -c "cd /workspace && $cmd"
    fi
}

build_image || { echo "Error: image build failed"; exit 1; }
echo "Image ready."

# In editable mode, binaries live in the bound host tree and may not exist yet (or may be
# stale after a source edit), so (incrementally) build them before any test runs.
if [ -n "$EDITABLE_FLAG" ]; then
    if [ -n "$TEST_FLAG$REGRESSION_FLAG$VARIANTS_FLAG" ]; then
        echo ""; echo "Editable: building release (+variants) into host cmake-build-release ..."
        run_in "./build.sh --release --variants --unit-tests"
    fi
    if [ -n "$TRACE_TEST_FLAG$TRACE_REGRESSION_FLAG$EXTRACT_TRACE_BINARY_FLAG" ]; then
        echo ""; echo "Editable: building trace config into host cmake-build-trace ..."
        run_in "./build.sh --trace"
    fi
fi

# ─── Test / action flags ─────────────────────────────────────────────────────
if [ -n "$TEST_FLAG" ]; then
    echo ""; echo "Running unit tests..."
    run_in "cd cmake-build-release && ctest -R '^unit_tests\$' --output-on-failure"
fi

if [ -n "$REGRESSION_FLAG" ]; then
    echo ""; echo "Running regression_level1..."
    run_in "cd cmake-build-release && ctest -R regression_level1 --output-on-failure"
fi

if [ -n "$VARIANTS_FLAG" ]; then
    echo ""; echo "Running regression_level1 for variant binaries..."
    run_in "cd cmake-build-release && ctest -R 'regression_level1_(flat|hash_only|lru|bitmap)' --output-on-failure"
fi

if [ -n "$TRACE_TEST_FLAG" ]; then
    echo ""; echo "Running unit_tests + trace identity/timeout CTests..."
    echo "(trace_regression excluded here — it needs the Linux reference binary; use --trace-regression)"
    run_in "cd cmake-build-trace && ctest -R '^unit_tests\$' --output-on-failure && ctest -R '^(trace_identity|trace_until_timeout)' --output-on-failure"
fi

if [ -n "$TRACE_REGRESSION_FLAG" ]; then
    echo ""
    if [ ! -f "$LINUX_REF_BIN_HOST" ]; then
        echo "Error: Linux reference binary not found at:"
        echo "  $LINUX_REF_BIN_HOST"
        echo "Run './scripts/container-build.sh --extract-trace-binary' first (and rename/place per KI-27)."
        exit 1
    fi
    echo "Running trace_regression_level1 (reference: $REF_BIN_NAME)..."
    if [ "$RUNTIME_KIND" = "apptainer" ]; then
        # apptainer: bind the reference binary's dir read-only and point the test at it.
        run_in "cd cmake-build-trace && ctest -R '^trace_regression_level1\$' --output-on-failure" \
               "--bind $(dirname "$LINUX_REF_BIN_HOST"):/05-Executables/reference:ro"
    else
        # OCI: the CLI may not preserve the exec bit on a mount, so copy+chmod inside.
        $CONTAINER_CMD run --rm -m "$MEMORY_LIMIT" \
            -v "${LINUX_REF_BIN_HOST}:/tmp/solvitaire-trace-ref-src:ro" \
            ${EDITABLE_FLAG:+-v "${REPO_ROOT}:/workspace"} \
            "$IMAGE_NAME" \
            bash -c "cd /workspace && mkdir -p '$(dirname "$LINUX_REF_BIN_CONTAINER")' && \
                     cp /tmp/solvitaire-trace-ref-src '${LINUX_REF_BIN_CONTAINER}' && \
                     chmod +x '${LINUX_REF_BIN_CONTAINER}' && \
                     cd cmake-build-trace && ctest -R '^trace_regression_level1\$' --output-on-failure"
    fi
fi

if [ -n "$EXTRACT_TRACE_BINARY_FLAG" ]; then
    echo ""; echo "Extracting solvitaire-trace binary..."
    if [ "$RUNTIME_KIND" = "apptainer" ]; then
        SRC="/workspace/cmake-build-trace/bin/solvitaire-trace"
        $CONTAINER_CMD exec ${EDITABLE_FLAG:+--bind "${REPO_ROOT}:/workspace"} \
            --bind "${REPO_ROOT}":/out "$SIF_NAME" \
            cp "$SRC" /out/solvitaire-trace-linux-amd64
    else
        EXTRACT_DIR=$(mktemp -d)
        $CONTAINER_CMD run --rm -v "${EXTRACT_DIR}:/output" "$IMAGE_NAME" \
            cp /workspace/cmake-build-trace/bin/solvitaire-trace /output/solvitaire-trace-linux-amd64
        mv "${EXTRACT_DIR}/solvitaire-trace-linux-amd64" ./solvitaire-trace-linux-amd64
        rm -rf "${EXTRACT_DIR}"
    fi
    echo "Binary extracted to: ./solvitaire-trace-linux-amd64"
    echo "NB: apptainer/'container' on Apple Silicon yields aarch64 despite the -amd64 name; verify with 'file'."
fi

if [ -z "$TEST_FLAG$REGRESSION_FLAG$VARIANTS_FLAG$TRACE_TEST_FLAG$TRACE_REGRESSION_FLAG$EXTRACT_TRACE_BINARY_FLAG" ]; then
    echo ""
    echo "Image built. To run something inside it:"
    if [ "$RUNTIME_KIND" = "apptainer" ]; then
        echo "  apptainer exec ${EDITABLE_FLAG:+--bind \"\$PWD\":/workspace }$SIF_NAME \\"
        echo "    bash -c 'cd /workspace && cmake-build-release/bin/solvitaire --type klondike --random 1 --json'"
        echo "  # benchmarks (bind an output dir):"
        echo "  apptainer exec --bind \"\$PWD\":/workspace --bind \"\$PWD/benchout\":/out $SIF_NAME \\"
        echo "    bash -c 'cd /workspace && scripts/experiments/bench_multiplicity.sh --phase D --games klondike --seeds 1-20 --outdir /out/run1'"
    else
        echo "  $CONTAINER_CMD run --rm -m $MEMORY_LIMIT $IMAGE_NAME \\"
        echo "    bash -c 'cd cmake-build-release && ctest -R unit_tests --output-on-failure'"
        echo "  $CONTAINER_CMD run --rm -it $IMAGE_NAME bash    # interactive"
    fi
fi
