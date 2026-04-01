# Plan: Cross-Platform `dev` Branch

**Goal:** A single `dev` branch that builds and tests reliably on both macOS (Apple Silicon) and Linux (x86_64 and arm64), replacing the current mac-centric `mac-dev` branch as the primary working branch.

---

## Current State

| Branch | Status |
|---|---|
| `mac-dev` | Primary working branch; has benchmarking, flat_cache, regression suite. All recent work is here. |
| `master` | Older baseline. Has a badly-formed Ubuntu 18.04 Dockerfile (invalid CMake syntax in it). |
| `reference-binaries` | Release-only; static binaries for Linux and macOS. No usable Dockerfile. |
| `refactor-caching` | Cache redesign work; documentation only at present. |

**The good news:** The source code currently has **no macOS-specific APIs**. The only platform split is already in `CMakeLists.txt` (lines 226–231):
```cmake
if(APPLE)
    set(CMAKE_CXX_FLAGS_RELEASE "... -O3 -flto -DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "... -O3 -flto -s -DNDEBUG -Wl,-O1")
endif()
```
This means `mac-dev` should already build on Linux — it just hasn't been verified or containerised.

---

## Branch Strategy

**Create `dev` directly from `mac-dev`** — no intermediate branch needed. The work is:
1. Verify the build works on Linux (via container)
2. Add a working Dockerfile
3. Add GitHub Actions CI
4. Document the setup

If platform-specific code is needed in future (e.g. mmap optimization), use `#ifdef __APPLE__` / `#ifdef __linux__` / `#ifdef _WIN32` inline — no separate branch needed for that.

The `mac-dev` and `mac-dev-benchmark-enhancements` branches can be retired once `dev` is verified stable. `refactor-caching` merges into `dev` when the cache redesign is ready.

---

## Implementation Tasks

### Task 1: Create the branch

```bash
git checkout mac-dev
git checkout -b dev
git push -u origin dev
```

### Task 2: Update `Dockerfile`

The old `Dockerfile` on `master` is broken (it has `cmake [-G ...]` with literal brackets, which is invalid shell syntax). Replace it with a working modern version.

**Target base image:** `ubuntu:22.04` (LTS, well-supported)

Required packages:
- `build-essential` (gcc, g++, make)
- `cmake` (≥3.14 — Ubuntu 22.04 ships 3.22)
- `libboost-program-options-dev` (only this component is needed, not `libboost-all-dev`)
- `git` (for GoogleTest FetchContent download)
- `python3` (for regression runner scripts)
- `ca-certificates` (for HTTPS git fetch of GoogleTest)

The Dockerfile should:
1. Install dependencies
2. Copy source
3. Run `./build.sh --release` and `./build.sh --release --unit-tests`
4. Run `cd cmake-build-release && ctest -R unit_tests` as a smoke test

**Approximate Dockerfile:**
```dockerfile
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libboost-program-options-dev \
    git \
    python3 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . /workspace/

RUN ./build.sh --release
RUN ./build.sh --release --unit-tests
RUN cd cmake-build-release && ctest -R unit_tests --output-on-failure

CMD ["/workspace/cmake-build-release/bin/solvitaire"]
```

**Note on GoogleTest:** `CMakeLists.txt` uses `FetchContent` to download GoogleTest v1.14.0 from GitHub at configure time. This requires internet access during `docker build`. The Dockerfile above handles this — just ensure the build machine has outbound HTTPS. If internet is unavailable, an alternative is to vendor GoogleTest into `lib/` and point `FetchContent_Declare` at a local path.

### Task 3: Add `docker-build.sh` helper script

A convenience script alongside `build.sh`:

```bash
#!/bin/bash
# Build and optionally run tests inside a Linux container
# Usage: ./docker-build.sh [--test]
#
# Requires: Docker or Podman

IMAGE_NAME="solvitaire-dev"
docker build -t "$IMAGE_NAME" .

if [ "$1" == "--test" ]; then
    docker run --rm "$IMAGE_NAME" \
        bash -c "cd cmake-build-release && ctest --output-on-failure -R unit_tests"
fi
```

Place at: `scripts/docker-build.sh`

### Task 4: Add GitHub Actions CI

Create `.github/workflows/ci.yml` to run on push and pull_request to `dev` and `master`.

**Matrix:** two jobs — macOS and Linux.

```yaml
name: CI

on:
  push:
    branches: [dev, master]
  pull_request:
    branches: [dev, master]

jobs:
  build-and-test:
    strategy:
      matrix:
        os: [ubuntu-22.04, macos-latest]

    runs-on: ${{ matrix.os }}

    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies (Linux)
        if: runner.os == 'Linux'
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake libboost-program-options-dev python3

      - name: Install dependencies (macOS)
        if: runner.os == 'macOS'
        run: brew install boost

      - name: Build release
        run: ./build.sh --release

      - name: Build unit tests
        run: ./build.sh --release --unit-tests

      - name: Run unit tests
        run: cd cmake-build-release && ctest -R unit_tests --output-on-failure

      - name: Run regression level 1
        run: cd cmake-build-release && ctest -R regression_level1 --output-on-failure
```

**Notes:**
- `macos-latest` on GitHub Actions is currently macOS 14 (Apple Silicon M1) — matches the development machine
- `ubuntu-22.04` is the LTS version matching the Dockerfile
- Level 1 regression (~2 min) is feasible in CI; levels 2–5 are too slow and should be run manually

### Task 5: Verify and fix any Linux-specific build issues

Known potential issues to check when first building on Linux:

1. **`-flto` + GCC:** Link-time optimisation with GCC sometimes requires `gcc-ar` and `gcc-ranlib` on the PATH. Add to CMakeLists.txt if needed:
   ```cmake
   if(NOT APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
       set(CMAKE_AR "gcc-ar")
       set(CMAKE_RANLIB "gcc-ranlib")
   endif()
   ```

2. **Boost detection:** Ubuntu 22.04's `libboost-program-options-dev` installs to `/usr/lib/x86_64-linux-gnu/` which CMake's `find_package(Boost)` should find automatically. If not, may need `set(BOOST_ROOT /usr)`.

3. **GCC warnings:** The codebase uses `-Werror` with GCC-specific warning flags (`-Wlogical-op`, `-Wnoexcept`, `-Wstrict-null-sentinel`). These are already conditioned on `CMAKE_CXX_COMPILER_ID STREQUAL "GNU"` in CMakeLists.txt lines 202–210 — should be fine.

4. **`__SIZEOF_INT128__`:** Available on both GCC and Clang on 64-bit Linux. No issue.

5. **Memory measurement in benchmarks:** The benchmark JSON outputs (`report-*.json`) include `max_resident_memory_bytes` and `system_memory_bytes` fields. These were populated by an older version of the benchmarking code. The current `benchmark.cpp` does not contain platform-specific memory measurement code — verify these fields are still being populated correctly, or document that they are not supported on Linux if they used macOS-specific APIs.

### Task 6: Update `CLAUDE.md` and branch documentation

- Add Linux build instructions (Docker path + native path)
- Note that `dev` is now the primary branch
- Document the container workflow for Linux testing

---

## Testing the Container on Your Mac

Since you have a container environment available:

```bash
# Build the Linux image
./scripts/docker-build.sh

# Run unit tests inside the container
./scripts/docker-build.sh --test

# Interactive shell in container
docker run --rm -it solvitaire-dev bash
```

For the regression suite inside the container:
```bash
docker run --rm solvitaire-dev \
    bash -c "cd cmake-build-release && ctest -R regression_level1 --output-on-failure"
```

---

## Future: Platform-Specific Code

When implementing the mmap lazy allocation optimization (documented in `docs/cache-redesign/optimization-opportunities.md`), do it **in `dev` directly** using `#ifdef` guards:

```cpp
// src/main/game/platform_memory.h  (new file)
#if defined(__APPLE__) || defined(__linux__)
#  include <sys/mman.h>
#elif defined(_WIN32)
#  include <windows.h>
#  include <memoryapi.h>
#endif
```

No separate mac/linux branch needed — the `#ifdef` approach keeps everything in one branch and the distinction is handled at compile time.

---

## Summary of Files to Create/Modify

| File | Action |
|---|---|
| `Dockerfile` | Replace broken existing one (from master) or create new |
| `scripts/docker-build.sh` | New — Linux container build helper |
| `.github/workflows/ci.yml` | New — GitHub Actions CI for macOS + Linux |
| `CLAUDE.md` | Update build instructions, note `dev` as primary branch |
| `CMakeLists.txt` | Minor: possibly add GCC LTO fix if needed |

No source code changes should be needed — just build infrastructure.

---

**Document Date:** 2026-04-01
**Branch:** refactor-caching (planning documentation)
**Target branch:** `dev` (to be created from `mac-dev`)
