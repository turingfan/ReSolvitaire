#!/usr/bin/env bash
# collect_results.sh — Collect benchmark results from a remote machine.
#
# Run this FROM YOUR LOCAL MACHINE. It SSHes to the remote, tars up the
# results directory, and scp's the tarball back to your local machine.
#
# Usage:
#   bash scripts/collect_results.sh --host user@server
#   bash scripts/collect_results.sh --host user@server --remote-dir results/20260407
#   bash scripts/collect_results.sh --host user@server --local-dir ~/benchmarks
#
# Options:
#   --host        user@server   SSH target (required)
#   --remote-dir  PATH          Results directory on remote (default: results)
#   --remote-root PATH          Root of repo on remote (default: ~/ReSolvitaire-caching)
#   --local-dir   PATH          Where to save the tarball locally (default: ~/Downloads)

set -euo pipefail

HOST=""
REMOTE_DIR="results"
REMOTE_ROOT="~/ReSolvitaire-caching"
LOCAL_DIR="$HOME/Downloads"

while [[ $# -gt 0 ]]; do
    case $1 in
        --host)        HOST="$2";        shift 2 ;;
        --remote-dir)  REMOTE_DIR="$2";  shift 2 ;;
        --remote-root) REMOTE_ROOT="$2"; shift 2 ;;
        --local-dir)   LOCAL_DIR="$2";   shift 2 ;;
        --help|-h)
            sed -n '2,16p' "$0"
            exit 0 ;;
        *)  echo "Unknown arg: $1"; exit 1 ;;
    esac
done

if [[ -z "$HOST" ]]; then
    echo "Error: --host user@server is required"
    echo "Usage: bash scripts/collect_results.sh --host user@server [--remote-dir results]"
    exit 1
fi

echo "[collect] Remote: $HOST"
echo "[collect] Remote path: $REMOTE_ROOT/$REMOTE_DIR"
echo "[collect] Local destination: $LOCAL_DIR"

# Create the tarball on the remote and print its path
TARBALL=$(ssh "$HOST" bash -s -- "$REMOTE_ROOT" "$REMOTE_DIR" <<'REMOTE_SCRIPT'
set -euo pipefail
REMOTE_ROOT="${1/#\~/$HOME}"
REMOTE_DIR="$2"

TARGET="$REMOTE_ROOT/$REMOTE_DIR"
if [[ ! -d "$TARGET" && ! -f "$TARGET" ]]; then
    echo "Error: $TARGET not found on remote" >&2
    exit 1
fi

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
HOSTNAME=$(hostname -s)
TARBALL="$REMOTE_ROOT/benchmark_${HOSTNAME}_${TIMESTAMP}.tar.gz"

echo "[collect] Creating $TARBALL ..." >&2
tar czf "$TARBALL" -C "$REMOTE_ROOT" "$REMOTE_DIR"
SIZE=$(du -sh "$TARBALL" | cut -f1)
echo "[collect] Done: $TARBALL ($SIZE)" >&2

# Print just the tarball path to stdout (captured by local script)
echo "$TARBALL"
REMOTE_SCRIPT
)

if [[ -z "$TARBALL" ]]; then
    echo "Error: failed to create tarball on remote"
    exit 1
fi

mkdir -p "$LOCAL_DIR"
LOCAL_FILE="$LOCAL_DIR/$(basename "$TARBALL")"

echo "[collect] Copying $HOST:$TARBALL -> $LOCAL_FILE ..."
scp "$HOST:$TARBALL" "$LOCAL_FILE"

echo ""
echo "[collect] Done: $LOCAL_FILE"
echo "[collect] To analyse:"
echo "  Rscript analysis/summary.R $LOCAL_FILE"
echo "  # or extract first: tar xzf $LOCAL_FILE -C /tmp/"
