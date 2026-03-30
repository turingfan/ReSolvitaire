#!/bin/bash
#
# Build a clean Release binary for a given branch and archive it
# Usage: ./scripts/build-branch.sh <branch-name>
#
# Example: ./scripts/build-branch.sh mac-dev
#
# The script:
# 1. Stashes any uncommitted changes in current branch
# 2. Checks out the target branch
# 3. Performs a clean release build
# 4. Copies executable to build-archive/ with naming:
#    solvitaire-<BRANCH>-<YYYYMMDD>-<7-char-hash>
# 5. Returns to original branch
# 6. Unstashes changes if any were stashed
#

set -e

# Check arguments
if [ -z "$1" ]; then
    echo "Usage: $0 <branch-name>"
    echo "Example: $0 mac-dev"
    exit 1
fi

TARGET_BRANCH="$1"

# Get current branch for later restoration
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
STASHED=0

echo "Building branch: $TARGET_BRANCH"
echo "Current branch: $CURRENT_BRANCH"

# Stash any uncommitted changes
if [ -n "$(git status --porcelain)" ]; then
    echo "Stashing uncommitted changes..."
    git stash push -m "Auto-stash before build-branch.sh"
    STASHED=1
fi

# Checkout target branch
echo "Checking out $TARGET_BRANCH..."
git checkout "$TARGET_BRANCH"

# Get commit hash (7 characters) and date
COMMIT_HASH=$(git rev-parse --short=7 HEAD)
BUILD_DATE=$(date +%Y%m%d)

# Extract branch name (in case it's origin/branch, use just the name)
BRANCH_NAME=$(echo "$TARGET_BRANCH" | sed 's|.*/||')

# Ensure build-archive directory exists
mkdir -p build-archive

# Clean build
echo "Performing clean build..."
rm -rf cmake-build-release
./build.sh --release > /dev/null 2>&1

if [ ! -f cmake-build-release/bin/solvitaire ]; then
    echo "ERROR: Build failed or executable not found!"
    exit 1
fi

# Archive the executable
ARCHIVE_NAME="solvitaire-${BRANCH_NAME}-${BUILD_DATE}-${COMMIT_HASH}"
cp cmake-build-release/bin/solvitaire "build-archive/${ARCHIVE_NAME}"

echo "✓ Built and archived: build-archive/${ARCHIVE_NAME}"

# Return to original branch
echo "Returning to original branch: $CURRENT_BRANCH"
git checkout "$CURRENT_BRANCH"

# Unstash if we stashed
if [ $STASHED -eq 1 ]; then
    echo "Unstashing changes..."
    git stash pop
fi

echo "Done!"
