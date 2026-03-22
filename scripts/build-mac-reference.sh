#!/bin/bash
# Script to build the macOS reference version of Solvitaire

set -e

# Configuration
OUTPUT_DIR="$(pwd)/bin/mac"
BUILD_DIR="build-mac"

mkdir -p "$OUTPUT_DIR"
mkdir -p "$BUILD_DIR"

echo "### Configuring macOS reference build..."
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Release -DBoost_USE_STATIC_LIBS=ON ..

echo "### Compiling..."
make -j$(sysctl -n hw.ncpu)

echo "### Copying binary to $OUTPUT_DIR/solvitaire-mac-arm64..."
cp bin/solvitaire "$OUTPUT_DIR/solvitaire-mac-arm64"

echo "### Success! macOS binary is available at $OUTPUT_DIR/solvitaire-mac-arm64"
