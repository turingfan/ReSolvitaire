#!/bin/bash
# Build and optionally run tests inside a Linux container
# Usage: ./scripts/container-build.sh [--test] [--regression]
#
# Requires: Docker, Podman, or the 'container' CLI (OCI-compliant container runtime)
#
# Memory note: the flat_cache uses mmap to reserve a large virtual address space
# (default 100M entries = 3.2 GB). Container runtimes often enforce a virtual
# memory limit that causes mmap to fail with std::bad_alloc. Tests are therefore
# run with -m 2g to ensure sufficient address space.

IMAGE_NAME="solvitaire-dev"
TEST_FLAG=""
REGRESSION_FLAG=""
VARIANTS_FLAG=""
TRACE_TEST_FLAG=""
TRACE_REGRESSION_FLAG=""
EXTRACT_TRACE_BINARY_FLAG=""
# Conventional location of the Linux reference binary (relative to repo root).
# cmake-build-trace configures its TRACE_REF_BIN to this path inside the container.
LINUX_REF_BIN_HOST="$(cd "$(dirname "$0")/.." && pwd)/../../05-Executables/reference/solvitaire-trace-reference-linux-arm64-20260529-ff68bde"
LINUX_REF_BIN_CONTAINER="/05-Executables/reference/solvitaire-trace-reference-linux-arm64-20260529-ff68bde"
# Default to --no-cache: the 'container' CLI v0.9 does not reliably
# invalidate the COPY layer when source files change, so cached builds
# silently use stale sources. Use --use-cache to opt in to caching
# (saves ~30s on apt install, useful on slow networks).
#
# IMPORTANT: --no-cache alone is NOT sufficient. The BuildKit builder
# maintains its own context cache that persists across builds. If the
# build uses stale source files despite --no-cache, run:
#
#   container builder delete --force
#
# This destroys and recreates the BuildKit container, clearing all
# cached build contexts. The next build will be slower (full apt
# install) but will see the current source files.
NO_CACHE_FLAG="--no-cache"
# Memory limit for test runs: 7g required due to mmap virtual address reservation
# (default flat_cache = 100M entries × 64 bytes = 6.4 GB virtual).
MEMORY_LIMIT="7g"

print_usage() {
    cat << EOF
Usage: ./scripts/container-build.sh [OPTIONS]

Options:
  --test                 Run unit tests after build (cmake-build-release)
  --regression           Run regression_level1 tests after build (main binary only)
  --variants             Run regression_level1 tests for all three variant binaries
                         (solvitaire-flat, solvitaire-hash-only, solvitaire-lru)
  --trace-test           Run unit_tests + trace_identity + trace_until_timeout from cmake-build-trace
                         (excludes trace_regression — those need the Linux reference binary)
  --trace-regression     Run trace_regression_level1 inside the container using the Linux reference
                         binary from 05-Executables/reference/ (must exist; run --extract-trace-binary
                         on the reference build first to create it)
  --extract-trace-binary Copy solvitaire-trace binary from container to ./solvitaire-trace-linux-amd64
  --use-cache            Allow cached layers (faster on slow networks, but may use stale sources)
  (no options)           Build the container image only

The container image is tagged as '$IMAGE_NAME' and requires a container
runtime (the 'container' CLI, Docker, or Podman) to be available on PATH.

Note: test runs use -m $MEMORY_LIMIT due to mmap virtual address reservation by the
flat_cache (default capacity reserves ~3.2 GB of virtual address space).

Examples:
  ./scripts/container-build.sh              # Build only
  ./scripts/container-build.sh --test       # Build and run unit tests
  ./scripts/container-build.sh --regression # Build and run Level 1 regression (main binary)
  ./scripts/container-build.sh --variants   # Build and run Level 1 regression for all variants
  ./scripts/container-build.sh --use-cache --test  # Faster build using cached layers
EOF
}

# Parse arguments
for arg in "$@"; do
    case "$arg" in
        --test)                 TEST_FLAG="1" ;;
        --regression)           REGRESSION_FLAG="1" ;;
        --variants)             VARIANTS_FLAG="1" ;;
        --trace-test)           TRACE_TEST_FLAG="1" ;;
        --trace-regression)     TRACE_REGRESSION_FLAG="1" ;;
        --extract-trace-binary) EXTRACT_TRACE_BINARY_FLAG="1" ;;
        --use-cache)            NO_CACHE_FLAG="" ;;
        --help|-h)    print_usage; exit 0 ;;
        *)
            echo "Unknown argument: $arg"
            print_usage
            exit 1
            ;;
    esac
done

# Determine which container runtime to use (try container, docker, podman in order)
CONTAINER_CMD=""
if command -v container &> /dev/null; then
    CONTAINER_CMD="container"
elif command -v docker &> /dev/null; then
    CONTAINER_CMD="docker"
elif command -v podman &> /dev/null; then
    CONTAINER_CMD="podman"
else
    echo "Error: No container runtime found. Please install Docker, Podman, or the container CLI."
    exit 1
fi

echo "Using container runtime: $CONTAINER_CMD"

# Build the image.
# CACHEBUST=$(date +%s) ensures the COPY layer and everything after it
# is never reused from cache, so source changes are always picked up.
# Note: if BuildKit still serves stale files, run:
#   container builder delete --force
echo "Building image '$IMAGE_NAME'..."
$CONTAINER_CMD build $NO_CACHE_FLAG --build-arg CACHEBUST="$(date +%s)" -t "$IMAGE_NAME" .

if [ $? -ne 0 ]; then
    echo "Error: Failed to build container image"
    exit 1
fi

echo "Image built successfully: $IMAGE_NAME"

# Run tests if requested
if [ -n "$TEST_FLAG" ]; then
    echo ""
    echo "Running unit tests inside container (memory limit: $MEMORY_LIMIT)..."
    $CONTAINER_CMD run --rm -m "$MEMORY_LIMIT" "$IMAGE_NAME" \
        bash -c "cd cmake-build-release && ctest -R '^unit_tests$' --output-on-failure"
fi

if [ -n "$REGRESSION_FLAG" ]; then
    echo ""
    echo "Running regression_level1 inside container (memory limit: $MEMORY_LIMIT)..."
    $CONTAINER_CMD run --rm -m "$MEMORY_LIMIT" "$IMAGE_NAME" \
        bash -c "cd cmake-build-release && ctest -R regression_level1 --output-on-failure"
fi

if [ -n "$VARIANTS_FLAG" ]; then
    echo ""
    echo "Running regression_level1 for all variant binaries inside container (memory limit: $MEMORY_LIMIT)..."
    $CONTAINER_CMD run --rm -m "$MEMORY_LIMIT" "$IMAGE_NAME" \
        bash -c "cd cmake-build-release && ctest -R 'regression_level1_(flat|hash_only|lru)' --output-on-failure"
fi

if [ -n "$TRACE_TEST_FLAG" ]; then
    echo ""
    echo "Running unit_tests and trace identity/timeout CTests inside container (memory limit: $MEMORY_LIMIT)..."
    echo "(trace_regression tests are excluded here — they need the Linux reference binary; use --extract-trace-binary first)"
    $CONTAINER_CMD run --rm -m "$MEMORY_LIMIT" "$IMAGE_NAME" \
        bash -c "cd cmake-build-trace && ctest -R '^unit_tests$' --output-on-failure && ctest -R '^(trace_identity|trace_until_timeout)' --output-on-failure"
fi

if [ -n "$TRACE_REGRESSION_FLAG" ]; then
    echo ""
    if [ ! -f "$LINUX_REF_BIN_HOST" ]; then
        echo "Error: Linux reference binary not found at:"
        echo "  $LINUX_REF_BIN_HOST"
        echo "Run './scripts/container-build.sh --extract-trace-binary' on the reference build first."
        exit 1
    fi
    echo "Running trace_regression_level1 inside container (memory limit: $MEMORY_LIMIT)..."
    echo "  Reference binary: $LINUX_REF_BIN_HOST"
    # Mount the reference binary to a temp path, then copy+chmod inside the container.
    # Direct mounting to the final path works for file access but the Apple 'container'
    # CLI does not preserve the execute bit on volume mounts, so the binary cannot be
    # run directly from the mount point.
    $CONTAINER_CMD run --rm -m "$MEMORY_LIMIT" \
        -v "${LINUX_REF_BIN_HOST}:/tmp/solvitaire-trace-ref-src:ro" \
        "$IMAGE_NAME" \
        bash -c "mkdir -p '$(dirname "$LINUX_REF_BIN_CONTAINER")' && \
                 cp /tmp/solvitaire-trace-ref-src '${LINUX_REF_BIN_CONTAINER}' && \
                 chmod +x '${LINUX_REF_BIN_CONTAINER}' && \
                 cd cmake-build-trace && ctest -R '^trace_regression_level1\$' --output-on-failure"
fi

if [ -n "$EXTRACT_TRACE_BINARY_FLAG" ]; then
    echo ""
    echo "Extracting solvitaire-trace binary from container..."
    EXTRACT_DIR=$(mktemp -d)
    $CONTAINER_CMD run --rm -v "${EXTRACT_DIR}:/output" "$IMAGE_NAME" \
        cp /workspace/cmake-build-trace/bin/solvitaire-trace /output/solvitaire-trace-linux-amd64
    mv "${EXTRACT_DIR}/solvitaire-trace-linux-amd64" ./solvitaire-trace-linux-amd64
    rm -rf "${EXTRACT_DIR}"
    echo "Binary extracted to: ./solvitaire-trace-linux-amd64"
fi

if [ -z "$TEST_FLAG" ] && [ -z "$REGRESSION_FLAG" ] && [ -z "$VARIANTS_FLAG" ] && [ -z "$TRACE_TEST_FLAG" ] && [ -z "$TRACE_REGRESSION_FLAG" ] && [ -z "$EXTRACT_TRACE_BINARY_FLAG" ]; then
    echo ""
    echo "To run tests in the container:"
    echo "  $CONTAINER_CMD run --rm -m $MEMORY_LIMIT $IMAGE_NAME \\"
    echo "    bash -c 'cd cmake-build-release && ctest -R unit_tests --output-on-failure'"
    echo ""
    echo "For an interactive shell:"
    echo "  $CONTAINER_CMD run --rm -it $IMAGE_NAME bash"
fi
