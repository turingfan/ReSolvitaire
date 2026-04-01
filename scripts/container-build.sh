#!/bin/bash
# Build and optionally run tests inside a Linux container
# Usage: ./scripts/container-build.sh [--test] [--regression]
#
# Requires: Docker, Podman, or the 'container' CLI (OCI-compliant container runtime)

IMAGE_NAME="solvitaire-dev"
TEST_FLAG=""
REGRESSION_FLAG=""
# Default to --no-cache: the 'container' CLI v0.9 does not reliably
# invalidate the COPY layer when source files change, so cached builds
# silently use stale sources. Use --use-cache to opt in to caching
# (saves ~30s on apt install, useful on slow networks).
NO_CACHE_FLAG="--no-cache"

print_usage() {
    cat << EOF
Usage: ./scripts/container-build.sh [OPTIONS]

Options:
  --test        Run unit tests after build
  --regression  Run regression_level1 tests after build
  --use-cache   Allow cached layers (faster on slow networks, but may use stale sources)
  (no options)  Build the container image only

The container image is tagged as '$IMAGE_NAME' and requires a container
runtime (the 'container' CLI, Docker, or Podman) to be available on PATH.

Examples:
  ./scripts/container-build.sh              # Build only
  ./scripts/container-build.sh --test       # Build and run unit tests
  ./scripts/container-build.sh --regression # Build and run Level 1 regression
  ./scripts/container-build.sh --use-cache --test  # Faster build using cached layers
EOF
}

# Parse arguments
for arg in "$@"; do
    case "$arg" in
        --test)       TEST_FLAG="1" ;;
        --regression) REGRESSION_FLAG="1" ;;
        --use-cache)  NO_CACHE_FLAG="" ;;
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
# This works even on container CLI v0.9 which ignores --no-cache.
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
    echo "Running unit tests inside container..."
    $CONTAINER_CMD run --rm "$IMAGE_NAME" \
        bash -c "cd cmake-build-release && ctest -R '^unit_tests$' --output-on-failure"
fi

if [ -n "$REGRESSION_FLAG" ]; then
    echo ""
    echo "Running regression_level1 inside container..."
    $CONTAINER_CMD run --rm "$IMAGE_NAME" \
        bash -c "cd cmake-build-release && ctest -R regression_level1 --output-on-failure"
fi

if [ -z "$TEST_FLAG" ] && [ -z "$REGRESSION_FLAG" ]; then
    echo ""
    echo "To run tests in the container:"
    echo "  $CONTAINER_CMD run --rm $IMAGE_NAME \\"
    echo "    bash -c 'cd cmake-build-release && ctest -R unit_tests --output-on-failure'"
    echo ""
    echo "For an interactive shell:"
    echo "  $CONTAINER_CMD run --rm -it $IMAGE_NAME bash"
fi
