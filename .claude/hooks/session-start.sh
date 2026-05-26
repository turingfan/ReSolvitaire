#!/bin/bash
set -euo pipefail

# Only run in remote (cloud) sessions
if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

# Install Boost program_options dev headers (required by CMakeLists.txt)
if ! dpkg -s libboost-program-options-dev >/dev/null 2>&1; then
  apt-get install -y libboost-program-options-dev
fi
