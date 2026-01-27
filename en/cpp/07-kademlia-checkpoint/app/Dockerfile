# Lesson 07: Kademlia Checkpoint (Final)
# Multi-stage build for cpp-libp2p workshop lesson

FROM --platform=linux/amd64 ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    libssl-dev \
    ninja-build \
    autoconf \
    automake \
    libtool \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt
RUN git clone --depth 1 https://github.com/libp2p/cpp-libp2p.git && \
    cd cpp-libp2p && \
    mkdir build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DTESTING=OFF \
        -DEXAMPLES=OFF \
        -DCMAKE_INSTALL_PREFIX=/opt/libp2p-install && \
    cmake --build . --parallel $(nproc) && \
    cmake --install .

WORKDIR /app

COPY main.cpp CMakeLists.txt ./

RUN HUNTER_DIR=$(find ~/.hunter -type d -name "Install" | head -1) && \
    mkdir build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="/opt/libp2p-install;${HUNTER_DIR}" && \
    cmake --build . --parallel $(nproc)

FROM --platform=linux/amd64 ubuntu:22.04

RUN apt-get update && apt-get install -y \
    ca-certificates \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/build/lesson /app/lesson

RUN ldconfig 2>/dev/null || true

ENV LISTEN_PORT=9000
ENV QUIC_PORT=9001
ENV REMOTE_PEERS=""
ENV BOOTSTRAP_PEERS=""

ARG TIMEOUT_DURATION=30s
ENV TIMEOUT_DURATION=${TIMEOUT_DURATION}

EXPOSE 9000/tcp
EXPOSE 9001/udp

CMD ["/bin/sh", "-c", "timeout ${TIMEOUT_DURATION} /app/lesson 2>&1 | tee /app/stdout.log"]
