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

# Remove any stale host build dirs (container CLI may not honour .dockerignore)
RUN rm -rf cmake-build-release cmake-build-debug build build-archive

# Two separate RUN steps so cmake configure is cached independently of compile
RUN cmake -DCMAKE_BUILD_TYPE=Release -Bcmake-build-release -H.
RUN cmake --build cmake-build-release --target solvitaire \
 && cmake --build cmake-build-release --target unit_tests

CMD ["/workspace/cmake-build-release/bin/solvitaire"]
