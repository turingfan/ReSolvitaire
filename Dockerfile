FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libboost-program-options-dev \
    git \
    python3 \
    ca-certificates \
    time \
    parallel \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# CACHEBUST ensures COPY and all subsequent layers are never cached.
# Passed automatically by container-build.sh as a Unix timestamp.
ARG CACHEBUST=1
RUN echo "Build timestamp: $CACHEBUST"

COPY . /workspace/

# Remove any stale host build dirs (container CLI may not honour .dockerignore)
RUN rm -rf cmake-build-release cmake-build-debug cmake-build-trace build build-archive

# Two separate RUN steps so cmake configure is cached independently of compile
RUN cmake -DCMAKE_BUILD_TYPE=Release -Bcmake-build-release -H.
RUN cmake --build cmake-build-release --parallel --target solvitaire \
 && cmake --build cmake-build-release --parallel --target solvitaire-flat \
 && cmake --build cmake-build-release --parallel --target solvitaire-hash-only \
 && cmake --build cmake-build-release --parallel --target solvitaire-lru \
 && cmake --build cmake-build-release --parallel --target solvitaire-bitmap \
 && cmake --build cmake-build-release --parallel --target unit_tests

# Build trace variant (for pre-merge validation against feature/search-trace reference binary).
# cmake-build-trace uses Release + SOLVITAIRE_TRACE=ON.
# Trace variant binaries: flat and lru needed for trace_identity_* tests;
# hash-only needed for SearchTraceAgreementTest and manual collision investigation.
RUN cmake -DCMAKE_BUILD_TYPE=Release -DSOLVITAIRE_TRACE=ON -Bcmake-build-trace -H. \
 && cmake --build cmake-build-trace --parallel --target solvitaire \
 && cmake --build cmake-build-trace --parallel --target solvitaire-trace \
 && cmake --build cmake-build-trace --parallel --target solvitaire-flat-trace \
 && cmake --build cmake-build-trace --parallel --target solvitaire-hash-only-trace \
 && cmake --build cmake-build-trace --parallel --target solvitaire-lru-trace \
 && cmake --build cmake-build-trace --parallel --target unit_tests

CMD ["/workspace/cmake-build-release/bin/solvitaire"]
