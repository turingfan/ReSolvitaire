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
