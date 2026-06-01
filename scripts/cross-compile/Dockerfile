# Dockerfile for Ter-Music cross-compilation environment
# Usage:
#   docker build -f scripts/cross-compile/Dockerfile -t ter-music-cross .
#   docker run --rm -v $(pwd):/workspace ter-music-cross ./scripts/build/build-deb.sh -a arm64
#
# NOTE: For version-flexible DEB builds across Debian 10/11/12/13,
# use Dockerfile.deb instead:
#   docker build --build-arg DEBIAN_VERSION=12 \
#     -f scripts/cross-compile/Dockerfile.deb \
#     -t ter-music-deb .

FROM debian:13

ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# Configure USTC mirror sources for Debian 13 (trixie)
RUN rm -f /etc/apt/sources.list.d/debian.sources && \
    echo 'Types: deb' > /etc/apt/sources.list.d/ustc.sources && \
    echo 'URIs: http://mirrors.ustc.edu.cn/debian' >> /etc/apt/sources.list.d/ustc.sources && \
    echo 'Suites: trixie trixie-updates' >> /etc/apt/sources.list.d/ustc.sources && \
    echo 'Components: main contrib non-free non-free-firmware' >> /etc/apt/sources.list.d/ustc.sources && \
    echo 'Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg' >> /etc/apt/sources.list.d/ustc.sources && \
    echo '' >> /etc/apt/sources.list.d/ustc.sources && \
    echo 'Types: deb' >> /etc/apt/sources.list.d/ustc.sources && \
    echo 'URIs: http://mirrors.ustc.edu.cn/debian-security' >> /etc/apt/sources.list.d/ustc.sources && \
    echo 'Suites: trixie-security' >> /etc/apt/sources.list.d/ustc.sources && \
    echo 'Components: main contrib non-free non-free-firmware' >> /etc/apt/sources.list.d/ustc.sources && \
    echo 'Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg' >> /etc/apt/sources.list.d/ustc.sources

# Basic development tools
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    libcurl4-openssl-dev \
    rsync \
    && rm -rf /var/lib/apt/lists/*

# Project build dependencies (matching debian/control Build-Depends)
RUN apt-get update && apt-get install -y \
    libavcodec-dev \
    libavfilter-dev \
    libavformat-dev \
    libavutil-dev \
    libswresample-dev \
    libswscale-dev \
    libpng-dev \
    libjpeg-dev \
    libxml2-dev \
    libncurses-dev \
    libpulse-dev \
    libdbus-1-dev \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

# DEB packaging tools
RUN apt-get update && apt-get install -y \
    dpkg-dev \
    fakeroot \
    debhelper \
    devscripts \
    && rm -rf /var/lib/apt/lists/*

# ARM64 cross-compilation toolchain
RUN apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    && rm -rf /var/lib/apt/lists/*

# ARM64 development libraries
RUN dpkg --add-architecture arm64 && \
    apt-get update && apt-get install -y -o DPkg::options::="--force-overwrite" \
    libncurses-dev:arm64 \
    libdbus-1-dev:arm64 \
    libavcodec-dev:arm64 \
    libavformat-dev:arm64 \
    libswresample-dev:arm64 \
    libavutil-dev:arm64 \
    libavfilter-dev:arm64 \
    libswscale-dev:arm64 \
    libpng-dev:arm64 \
    libjpeg-dev:arm64 \
    libxml2-dev:arm64 \
    libcurl4-openssl-dev:arm64 \
    libpulse-dev:arm64 \
    libsqlite3-dev:arm64 \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# Copy the project source code
COPY . /workspace/

# Make build scripts executable
RUN chmod +x /workspace/scripts/build/*.sh

# Default command: build for ARM64
CMD ["./scripts/build/build-deb.sh", "-a", "arm64"]
