#!/usr/bin/env bash
# setup_remote.sh — Clone/pull ReSolvitaire and build on a remote machine.
#
# Usage:
#   First time:  bash scripts/setup_remote.sh --repo <github-url> [--dir <path>] [--branch benchmark-python] [--commit HEAD]
#   Subsequent:  bash scripts/setup_remote.sh [--dir <path>] [--branch benchmark-python] [--commit HEAD]
#
# The script is idempotent: safe to rerun. If the directory exists it pulls
# instead of cloning. Then builds release + unit-test binaries.

set -euo pipefail

REPO_URL=""
WORK_DIR="$HOME/ReSolvitaire-caching"
BRANCH="benchmark-python"
COMMIT="HEAD"

while [[ $# -gt 0 ]]; do
    case $1 in
        --repo)    REPO_URL="$2";   shift 2 ;;
        --dir)     WORK_DIR="$2";   shift 2 ;;
        --branch)  BRANCH="$2";     shift 2 ;;
        --commit)  COMMIT="$2";     shift 2 ;;
        *)         echo "Unknown arg: $1"; exit 1 ;;
    esac
done

echo "[setup] Target directory: $WORK_DIR"
echo "[setup] Branch: $BRANCH  Commit: $COMMIT"

# ── Clone or pull ─────────────────────────────────────────────────────────────
if [[ -d "$WORK_DIR/.git" ]]; then
    echo "[setup] Repository exists — fetching latest..."
    git -C "$WORK_DIR" fetch origin
else
    if [[ -z "$REPO_URL" ]]; then
        echo "Error: --repo <url> required for first-time clone"
        exit 1
    fi
    echo "[setup] Cloning $REPO_URL..."
    git clone "$REPO_URL" "$WORK_DIR"
fi

cd "$WORK_DIR"

git checkout "$BRANCH"
if [[ "$COMMIT" != "HEAD" ]]; then
    git checkout "$COMMIT"
    echo "[setup] Checked out commit $COMMIT"
else
    git pull origin "$BRANCH"
fi

ACTUAL_BRANCH=$(git rev-parse --abbrev-ref HEAD)
ACTUAL_COMMIT=$(git rev-parse --short HEAD)
echo "[setup] At: branch=$ACTUAL_BRANCH  commit=$ACTUAL_COMMIT"

# ── Dependencies ──────────────────────────────────────────────────────────────
if command -v apt-get &>/dev/null; then
    echo "[setup] Installing build dependencies (apt)..."
    sudo apt-get update -qq
    sudo apt-get install -y -qq cmake libboost-program-options-dev python3 python3-pip r-base
elif command -v brew &>/dev/null; then
    echo "[setup] Installing build dependencies (brew)..."
    brew install boost cmake r
fi

# ── Build ─────────────────────────────────────────────────────────────────────
unset CMAKE_GENERATOR CMAKE_GENERATOR_PLATFORM CMAKE_GENERATOR_TOOLSET

echo "[setup] Building release..."
./build.sh --release

echo "[setup] Building unit tests..."
./build.sh --release --unit-tests

# ── Smoke test ────────────────────────────────────────────────────────────────
echo "[setup] Smoke test..."
./cmake-build-release/bin/solvitaire --type free-cell --random 1 --json > /dev/null
echo "[setup] Smoke test passed."

# ── Results directory ─────────────────────────────────────────────────────────
mkdir -p results

echo ""
echo "[setup] Ready. To run benchmarks:"
echo "  python3 scripts/benchmark_orchestrator.py \\"
echo "      --solver cmake-build-release/bin/solvitaire \\"
echo "      --workers 32 \\"
echo "      --output-dir results/\$(date +%Y%m%d)"
echo ""
echo "  # Quick test (5 games, 50 seeds each):"
echo "  python3 scripts/benchmark_orchestrator.py \\"
echo "      --solver cmake-build-release/bin/solvitaire \\"
echo "      --workers 32 --quick \\"
echo "      --output-dir results/\$(date +%Y%m%d)_quick"
