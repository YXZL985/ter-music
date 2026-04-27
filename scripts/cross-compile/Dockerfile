# Dockerfile for Ter-Music cross-compilation environment
# Usage: 
#   docker build -f scripts/cross-compile/Dockerfile -t ter-music-cross .
#   docker run --rm -v $(pwd):/workspace ter-music-cross ./scripts/build/build-deb.sh -a arm64

FROM debian:13

ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# Remove default sources and configure USTC mirror sources for Debian 13 (trixie)
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

# Install basic tools
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    rsync \
    && rm -rf /var/lib/apt/lists/*

# Install cross-compilation toolchain for ARM64
RUN apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    && rm -rf /var/lib/apt/lists/*

# Add ARM64 architecture and install development libraries
RUN dpkg --add-architecture arm64 && \
    apt-get update && apt-get install -y \
    libncurses-dev:arm64 \
    libdbus-1-dev:arm64 \
    libavcodec-dev:arm64 \
    libavformat-dev:arm64 \
    libswresample-dev:arm64 \
    libavutil-dev:arm64 \
    libpulse-dev:arm64 \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Install build dependencies for each package format
# DEB packages
RUN apt-get update && apt-get install -y \
    dpkg-dev \
    fakeroot \
    debhelper \
    && rm -rf /var/lib/apt/lists/*

# RPM packages
RUN apt-get update && apt-get install -y \
    rpm \
    && rm -rf /var/lib/apt/lists/*

# AppImage dependencies
RUN apt-get update && apt-get install -y \
    squashfs-tools \
    file \
    && rm -rf /var/lib/apt/lists/*

# Linyaps (linglong) - install from Deepin OBS repository
# Install xdg-utils first for xdg-mime command required by linglong-installer
RUN apt-get update && apt-get install -y xdg-utils && rm -rf /var/lib/apt/lists/*
RUN echo "deb [trusted=yes] https://ci.deepin.com/repo/obs/linglong:/CI:/release/Debian_13/ ./" > /etc/apt/sources.list.d/linglong.list && \
    apt-get update && \
    apt-get install -y linglong-bin linglong-installer linglong-builder && \
    rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /workspace

# Copy the project source code
COPY . /workspace/

# Make build scripts executable
RUN chmod +x /workspace/scripts/build/*.sh

# Default command: build for ARM64
CMD ["./scripts/build/build-deb.sh", "-a", "arm64"]
