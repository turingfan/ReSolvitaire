#!/usr/bin/env bash
# collect_results.sh — Tar up benchmark results and print the scp command.
#
# Run this ON THE REMOTE MACHINE after benchmarks complete.
#
# Usage:
#   bash scripts/collect_results.sh [results/20260407]
#   bash scripts/collect_results.sh          # tars all of results/

set -euo pipefail

TARGET="${1:-results}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
HOSTNAME=$(hostname -s)
TARBALL="benchmark_${HOSTNAME}_${TIMESTAMP}.tar.gz"

echo "[collect] Tarring $TARGET -> $TARBALL ..."
tar czf "$TARBALL" "$TARGET"
SIZE=$(du -sh "$TARBALL" | cut -f1)
echo "[collect] Done: $TARBALL ($SIZE)"

echo ""
echo "Copy to your local machine with:"
echo "  scp ${HOSTNAME}:$(pwd)/${TARBALL} ~/Downloads/"
echo ""
echo "Or if you need to specify user/host:"
echo "  scp user@<remote-host>:$(pwd)/${TARBALL} ~/Downloads/"
