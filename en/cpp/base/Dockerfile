# Base image with cpp-libp2p pre-built
# Build once, use for all lessons
FROM --platform=linux/amd64 ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies including Clang
RUN apt-get update && apt-get install -y \
    build-essential \
    clang \
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
    ca-certificates \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

# Clone and build cpp-libp2p with Hunter using Clang
WORKDIR /opt
RUN git clone --depth 1 https://github.com/libp2p/cpp-libp2p.git && \
    cd cpp-libp2p && \
    # Fix the buggy -Werror- flags to -Werror= in all cmake files
    find . -name "*.cmake" -o -name "CMakeLists.txt" | xargs sed -i 's/-Werror-/-Werror=/g' && \
    mkdir build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DTESTING=OFF \
        -DEXAMPLES=OFF \
        -DCMAKE_INSTALL_PREFIX=/opt/libp2p-install || \
    (echo "=== CMakeError.log ===" && cat CMakeFiles/CMakeError.log && exit 1) && \
    cmake --build . --parallel $(nproc) && \
    cmake --install .

# Store Hunter directory path for lessons to use
RUN HUNTER_DIR=$(find ~/.hunter -type d -name "Install" | head -1) && \
    echo "${HUNTER_DIR}" > /opt/hunter_path.txt

WORKDIR /app
