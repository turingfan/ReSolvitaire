#!/usr/bin/env bash
# setup_remote.sh — Set up ReSolvitaire on a remote machine from your local machine.
#
# Run this FROM YOUR LOCAL MACHINE. It SSHes into the remote and performs
# clone/pull + build there. No need to log in manually.
#
# Usage (first time):
#   bash scripts/setup_remote.sh --host user@server --repo <github-url>
#
# Usage (subsequent — pull latest and rebuild):
#   bash scripts/setup_remote.sh --host user@server
#
# Options:
#   --host    user@server   SSH target (required)
#   --repo    URL           GitHub URL for first-time clone (required on first run)
#   --dir     PATH          Remote working directory (default: ~/ReSolvitaire-caching)
#   --branch  BRANCH        Branch to check out (default: dev)
#   --commit  SHA           Pin to a specific commit instead of HEAD

set -euo pipefail

HOST=""
REPO_URL=""
WORK_DIR="~/ReSolvitaire-caching"
BRANCH="dev"
COMMIT="HEAD"

while [[ $# -gt 0 ]]; do
    case $1 in
        --host)    HOST="$2";      shift 2 ;;
        --repo)    REPO_URL="$2";  shift 2 ;;
        --dir)     WORK_DIR="$2";  shift 2 ;;
        --branch)  BRANCH="$2";    shift 2 ;;
        --commit)  COMMIT="$2";    shift 2 ;;
        --help|-h)
            sed -n '2,20p' "$0"
            exit 0 ;;
        *)  echo "Unknown arg: $1"; exit 1 ;;
    esac
done

if [[ -z "$HOST" ]]; then
    echo "Error: --host user@server is required"
    echo "Usage: bash scripts/setup_remote.sh --host user@server [--repo URL] [--branch dev]"
    exit 1
fi

echo "[setup] Remote: $HOST  Dir: $WORK_DIR  Branch: $BRANCH"

# Build the remote script as a heredoc and pipe it to ssh.
# This avoids copying the script file separately.
ssh "$HOST" bash -s -- "$WORK_DIR" "$BRANCH" "$COMMIT" "$REPO_URL" <<'REMOTE_SCRIPT'
set -euo pipefail
WORK_DIR="$1"
BRANCH="$2"
COMMIT="$3"
REPO_URL="$4"

echo "[setup] Target directory: $WORK_DIR"
echo "[setup] Branch: $BRANCH  Commit: $COMMIT"

# Expand ~ manually since it may be passed as a literal string
WORK_DIR="${WORK_DIR/#\~/$HOME}"

# ── Clone or pull ──────────────────────────────────────────────────────────
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
    echo "[setup] Pinned to commit $COMMIT"
else
    git pull origin "$BRANCH"
fi

ACTUAL_COMMIT=$(git rev-parse --short HEAD)
echo "[setup] At: branch=$BRANCH  commit=$ACTUAL_COMMIT"

# ── Dependencies ──────────────────────────────────────────────────────────
if command -v apt-get &>/dev/null; then
    echo "[setup] Installing build dependencies (apt)..."
    sudo apt-get update -qq
    sudo apt-get install -y -qq cmake libboost-program-options-dev python3 r-base
elif command -v brew &>/dev/null; then
    echo "[setup] Installing build dependencies (brew)..."
    brew install boost cmake r
fi

# ── Build ─────────────────────────────────────────────────────────────────
unset CMAKE_GENERATOR CMAKE_GENERATOR_PLATFORM CMAKE_GENERATOR_TOOLSET 2>/dev/null || true

echo "[setup] Configuring cmake..."
cmake -DCMAKE_BUILD_TYPE=Release -Bcmake-build-release -H.

echo "[setup] Building all targets..."
cmake --build cmake-build-release --target solvitaire
cmake --build cmake-build-release --target solvitaire-flat
cmake --build cmake-build-release --target solvitaire-hash-only
cmake --build cmake-build-release --target solvitaire-lru

# ── Smoke test ────────────────────────────────────────────────────────────
echo "[setup] Smoke test..."
./cmake-build-release/bin/solvitaire --type free-cell --random 1 --json > /dev/null
echo "[setup] Smoke test passed."

# ── Results directory ─────────────────────────────────────────────────────
mkdir -p results

echo ""
echo "[setup] Ready at $WORK_DIR on $(hostname -s)."
echo "[setup] To run benchmarks:"
echo "  python3 scripts/benchmark_orchestrator.py \\"
echo "      --solver cmake-build-release/bin/solvitaire \\"
echo "      --workers \$(nproc) \\"
echo "      --output-dir results/\$(date +%Y%m%d)"
REMOTE_SCRIPT
