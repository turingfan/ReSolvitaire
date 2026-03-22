#!/bin/bash
# Script to build the Linux reference version of Solvitaire using Apple's container CLI

set -e

# Configuration
IMAGE_NAME="solvitaire-linux-reference"
DOCKERFILE="Dockerfile.linux"
OUTPUT_DIR="$(pwd)/bin/linux"

# Default engine is Apple's container CLI
ENGINE="container"

# Allow user to choose different container engine via -e or --engine
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -e|--engine) ENGINE="$2"; shift ;;
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

echo "### Building Linux reference image using $ENGINE..."
"$ENGINE" build --tag "$IMAGE_NAME" --file "$DOCKERFILE" .

echo "### Extracting binary from container to $OUTPUT_DIR/solvitaire-linux-arm64..."
"$ENGINE" run -v "$OUTPUT_DIR":/output --rm "$IMAGE_NAME" cp /app/linux-build/bin/solvitaire /output/solvitaire-linux-arm64

echo "### Success! Linux binary is available at $OUTPUT_DIR/solvitaire-linux-arm64"
