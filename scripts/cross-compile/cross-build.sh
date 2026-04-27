#!/bin/bash
#
# Cross-compilation wrapper script
# Runs build scripts inside a Docker container with proper cross-compilation environment
#

set -e

cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1
SCRIPT_DIR="$(pwd)"
IMAGE_NAME="ter-music-cross"
CONTAINER_NAME="ter-music-cross-build"

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1" >&2
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

show_help() {
    cat << EOF
用法: $0 [选项] -- [构建脚本参数]

在 Docker 容器中进行交叉编译

选项:
    -h, --help          显示此帮助信息
    -b, --build-image   重新构建 Docker 镜像
    -s, --script SCRIPT 指定构建脚本 (默认: build-deb.sh)
    -a, --arch ARCH     指定目标架构 (默认: arm64)
    -i, --interactive   进入容器的交互式 shell
    --no-cache          构建镜像时不使用缓存

示例:
    $0                          # 使用默认设置构建 arm64 DEB 包
    $0 -a arm64                 # 构建 arm64 DEB 包
    $0 -s build-rpm.sh         # 使用 RPM 构建脚本
    $0 -s build-appimage.sh -a aarch64  # 构建 aarch64 AppImage
    $0 -i                       # 进入交互式 shell
    $0 -b                       # 重新构建 Docker 镜像
    $0 -- --keep-temp           # 传递参数给构建脚本

EOF
}

# Parse arguments
BUILD_IMAGE=false
SCRIPT="build-deb.sh"
TARGET_ARCH="arm64"
INTERACTIVE=false
NO_CACHE=""
BUILD_SCRIPT_ARGS=()

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -b|--build-image)
            BUILD_IMAGE=true
            shift
            ;;
        -s|--script)
            SCRIPT="$2"
            shift 2
            ;;
        -a|--arch)
            TARGET_ARCH="$2"
            shift 2
            ;;
        -i|--interactive)
            INTERACTIVE=true
            shift
            ;;
        --no-cache)
            NO_CACHE="--no-cache"
            shift
            ;;
        --)
            shift
            BUILD_SCRIPT_ARGS=("$@")
            break
            ;;
        *)
            BUILD_SCRIPT_ARGS+=("$1")
            shift
            ;;
    esac
done

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    log_error "Docker 未安装，请先安装 Docker"
    log_info "安装命令: sudo apt install docker.io"
    exit 1
fi

# Build Docker image if needed
if [ "$BUILD_IMAGE" = true ] || ! docker image inspect "$IMAGE_NAME" &> /dev/null; then
    log_info "构建 Docker 镜像: $IMAGE_NAME"
    docker build $NO_CACHE -f scripts/cross-compile/Dockerfile -t "$IMAGE_NAME" "$SCRIPT_DIR"
    if [ $? -ne 0 ]; then
        log_error "Docker 镜像构建失败"
        exit 1
    fi
    log_info "Docker 镜像构建完成"
else
    log_info "使用已存在的 Docker 镜像: $IMAGE_NAME"
fi

# Run container
if [ "$INTERACTIVE" = true ]; then
    log_info "进入交互式容器..."
    docker run --rm -it \
        -v "$SCRIPT_DIR":/workspace \
        --workdir /workspace \
        "$IMAGE_NAME" \
        /bin/bash
else
    log_info "在容器中运行构建脚本: $SCRIPT"
    log_info "目标架构: $TARGET_ARCH"
    log_info "构建参数: ${BUILD_SCRIPT_ARGS[*]}"
    
    docker run --rm \
        -v "$SCRIPT_DIR":/workspace \
        --workdir /workspace \
        --privileged \
        "$IMAGE_NAME" \
        ./scripts/build/$SCRIPT -a "$TARGET_ARCH" "${BUILD_SCRIPT_ARGS[@]}"
    
    if [ $? -eq 0 ]; then
        log_info "构建完成！输出目录: ${SCRIPT_DIR}/build/"
    else
        log_error "构建失败"
        exit 1
    fi
fi
