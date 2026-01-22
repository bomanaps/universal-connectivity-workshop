# Multi-stage build for cpp-libp2p workshop lesson
# Use linux/amd64 platform for better compatibility (boringssl has ARM64 issues)

# Stage 1: Build environment with Hunter (cpp-libp2p's package manager)
FROM --platform=linux/amd64 ubuntu:22.04 AS builder

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
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

# Clone and build cpp-libp2p with Hunter (it manages all dependencies)
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

# Build the lesson application
WORKDIR /app

# Copy source files
COPY main.cpp CMakeLists.txt ./

# Get Hunter install directory (it's created during cpp-libp2p build)
# Hunter caches dependencies in ~/.hunter
RUN HUNTER_DIR=$(find ~/.hunter -type d -name "Install" | head -1) && \
    mkdir build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="/opt/libp2p-install;${HUNTER_DIR}" && \
    cmake --build . --parallel $(nproc)

# Stage 2: Runtime environment (must match builder platform)
FROM --platform=linux/amd64 ubuntu:22.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    ca-certificates \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the built binary from builder
COPY --from=builder /app/build/lesson /app/lesson

# cpp-libp2p is statically linked with Hunter, so we shouldn't need shared libraries
# But if needed, copy them:
# COPY --from=builder /opt/libp2p-install/lib/*.so* /usr/local/lib/ 2>/dev/null || true

# Update library cache (in case we copied any shared libs)
RUN ldconfig 2>/dev/null || true

# Configurable timeout duration
ARG TIMEOUT_DURATION=10s
ENV TIMEOUT_DURATION=${TIMEOUT_DURATION}

# Set the command to run with timeout and output to stdout.log
CMD ["/bin/sh", "-c", "timeout ${TIMEOUT_DURATION} /app/lesson 2>&1 | tee /app/stdout.log"]
