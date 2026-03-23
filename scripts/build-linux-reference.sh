#!/bin/bash
# Script to build the Linux reference version of Solvitaire using Apple's container CLI

set -e

# Configuration
IMAGE_NAME="solvitaire-linux-reference"
DOCKERFILE="Dockerfile.linux"
OUTPUT_DIR="$(pwd)/bin/linux"

# Default engine is Apple's container CLI
ENGINE="container"
ARCHS=("arm64" "amd64")

# Allow user to choose different container engine via -e or --engine
# and specific architecture via -a or --arch
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -e|--engine) ENGINE="$2"; shift ;;
        -a|--arch) ARCHS=("$2"); shift ;;
        *) echo "Unknown parameter passed: $1"; exit 1 ;;
    esac
    shift
done

# Check if the chosen engine is available
if ! command -v "$ENGINE" &> /dev/null; then
    echo "Error: Container engine '$ENGINE' not found."
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

for ARCH in "${ARCHS[@]}"; do
    PLATFORM="linux/$ARCH"
    TAG="${IMAGE_NAME}:${ARCH}"
    BINARY_NAME="solvitaire-linux-${ARCH}"

    echo "### Building Linux ${ARCH} reference image using ${ENGINE} (--platform ${PLATFORM})..."
    "$ENGINE" build --platform "$PLATFORM" --tag "$TAG" --file "$DOCKERFILE" .

    echo "### Extracting binary from ${ARCH} container to ${OUTPUT_DIR}/${BINARY_NAME}..."
    "$ENGINE" run --platform "$PLATFORM" -v "$OUTPUT_DIR":/output --rm "$TAG" cp /app/build/bin/solvitaire /output/"${BINARY_NAME}"

    echo "### Success! Linux ${ARCH} binary is available at ${OUTPUT_DIR}/${BINARY_NAME}"
done
