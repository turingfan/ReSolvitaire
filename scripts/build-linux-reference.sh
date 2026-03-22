#!/bin/bash
# Script to build the Linux reference version of Solvitaire using Apple's container CLI

set -e

# Configuration
IMAGE_NAME="solvitaire-linux-reference"
DOCKERFILE="Dockerfile.linux"
OUTPUT_DIR="$(pwd)/bin/linux"

mkdir -p "$OUTPUT_DIR"

echo "### Building Linux reference image using Apple Container CLI..."
# We use . as the build context; it must contain the Dockerfile and source code
container build --tag "$IMAGE_NAME" --file "$DOCKERFILE" .

echo "### Extracting binary from container to $OUTPUT_DIR/solvitaire-linux..."
# We run the container and mount our local output directory to /output
# Then copy the binary from the /app inside the container into the shared mount
container run -v "$OUTPUT_DIR":/output --rm "$IMAGE_NAME" cp /app/build/bin/solvitaire /output/solvitaire-linux

echo "### Success! Linux binary is available at $OUTPUT_DIR/solvitaire-linux"
echo "Note: You can run this natively inside the container using: 'container run -it $IMAGE_NAME'"
