#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_NAME="ter-music"
DEFAULT_VERSION="1.0.0"
OUTPUT_DIR="${SCRIPT_DIR}/build/deb"
TEMP_DIR="${SCRIPT_DIR}/.debbuild_temp"

log_info() {
    echo "[INFO] $1"
}

log_error() {
    echo "[ERROR] $1" >&2
}

log_clean() {
    echo "[CLEAN] $1"
}

show_help() {
    cat << EOF
用法: $0 [选项]

将 ter-music 项目打包成标准的 Debian/Ubuntu DEB 包

选项:
    -h, --help          显示此帮助信息
    -v, --version VERSION  指定版本号（默认：自动检测）
    -k, --keep-temp     保留临时构建文件（用于调试）

示例:
    $0                  使用自动检测的版本号构建 DEB
    $0 -v 1.2.3         使用指定版本号 1.2.3 构建 DEB
    $0 --keep-temp      构建后保留临时文件

输出:
    DEB 包将输出到: ${OUTPUT_DIR}/

EOF
}

check_dependencies() {
    log_info "检查构建依赖..."

    local missing_deps=()

    if ! command -v dpkg-deb &> /dev/null; then
        missing_deps+=("dpkg-dev")
    fi

    if ! command -v fakeroot &> /dev/null; then
        missing_deps+=("fakeroot")
    fi

    if ! command -v gcc &> /dev/null; then
        missing_deps+=("gcc")
    fi

    if ! command -v make &> /dev/null; then
        missing_deps+=("make")
    fi

    if ! command -v cmake &> /dev/null; then
        missing_deps+=("cmake")
    fi

    if ! command -v tar &> /dev/null; then
        missing_deps+=("tar")
    fi

    if [ ${#missing_deps[@]} -gt 0 ]; then
        log_error "缺少以下构建工具:"
        for dep in "${missing_deps[@]}"; do
            echo "  - $dep"
        done
        echo ""
        log_error "请使用以下命令安装缺失的工具:"
        echo "  sudo apt install ${missing_deps[*]}"
        exit 1
    fi

    log_info "所有构建依赖已满足"
}

detect_version() {
    local version="$DEFAULT_VERSION"

    if [ -d "${SCRIPT_DIR}/.git" ] && command -v git >/dev/null 2>&1; then
        local git_version=$(git describe --tags --abbrev=0 2>/dev/null || true)
        if [[ $git_version =~ ^v?([0-9]+\.[0-9]+\.[0-9]+)$ ]]; then
            version="${BASH_REMATCH[1]}"
            echo "$version"
            return
        fi
    fi

    if [ -f "${SCRIPT_DIR}/include/defs.h" ]; then
        local match
        match=$(grep -E 'APP_VERSION' "${SCRIPT_DIR}/include/defs.h" | head -1)
        if [[ $match =~ ([0-9]+\.[0-9]+\.[0-9]+) ]]; then
            version="${BASH_REMATCH[1]}"
        fi
    fi

    if [ "$version" = "$DEFAULT_VERSION" ] && [ -f "${SCRIPT_DIR}/CMakeLists.txt" ]; then
        local match
        match=$(grep -E 'project.*VERSION' "${SCRIPT_DIR}/CMakeLists.txt" | head -1)
        if [[ $match =~ ([0-9]+\.[0-9]+\.[0-9]+) ]]; then
            version="${BASH_REMATCH[1]}"
        else
            match=$(grep -E 'set.*VERSION' "${SCRIPT_DIR}/CMakeLists.txt" | head -1)
            if [[ $match =~ ([0-9]+\.[0-9]+\.[0-9]+) ]]; then
                version="${BASH_REMATCH[1]}"
            fi
        fi
    fi

    echo "$version"
}

prepare_directories() {
    log_info "准备构建目录..."

    mkdir -p "${OUTPUT_DIR}"

    rm -rf "${TEMP_DIR}"
    mkdir -p "${TEMP_DIR}"

    log_clean "已清理并创建构建目录"
}

build_from_source() {
    local build_dir="$1"

    log_info "从源码构建..."

    mkdir -p "${build_dir}"
    cd "${build_dir}"

    cmake "${SCRIPT_DIR}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
    make -j$(nproc)

    log_info "源码构建完成"
}

create_deb_directory_structure() {
    local deb_root="$1"

    log_info "创建 DEB 目录结构..."

    mkdir -p "${deb_root}"/DEBIAN
    mkdir -p "${deb_root}"/usr/bin
    mkdir -p "${deb_root}"/usr/share/applications
    mkdir -p "${deb_root}"/usr/share/icons/hicolor/32x32/apps
    mkdir -p "${deb_root}"/usr/share/icons/hicolor/48x48/apps
    mkdir -p "${deb_root}"/usr/share/icons/hicolor/128x128/apps
    mkdir -p "${deb_root}"/usr/share/icons/hicolor/scalable/apps

    log_info "DEB 目录结构创建完成"
}

generate_control_file() {
    local deb_root="$1"
    local version="$2"
    local control_file="${deb_root}/DEBIAN/control"

    log_info "生成 DEBIAN/control 文件..."

    cat > "$control_file" << EOF
Package: ${PROJECT_NAME}
Version: ${version}
Architecture: amd64
Maintainer: Yanxi Bamboo Forest <maintainer@example.com>
Description: A terminal-based music player with ncurses interface
 Ter-Music is a lightweight terminal-based music player for Linux systems.
 It uses FFmpeg for audio decoding, PulseAudio/ALSA for audio output, and ncursesw
 for a beautiful text user interface.
 .
 Features:
  - Support for multiple audio formats (MP3, WAV, FLAC, OGG, M4A, AAC, WMA, APE, OPUS)
  - LRC lyrics synchronization display
  - Multiple playback modes (sequential, single loop, list loop, random)
  - Playlist management
  - Favorites collection
  - Playback history
  - Customizable color themes
  - Keyboard shortcuts
  - Real-time progress bar
Depends: libavcodec58 | libavcodec-ffmpeg56 | libavcodec-extra, libavformat58 | libavformat57, libavutil56 | libavutil55, libswresample3 | libswresample2, libpulse0 | libasound2, libncursesw5 | libncursesw6
Section: sound
Priority: optional
Homepage: https://gitee.com/yanxi-bamboo-forest/ter-music
EOF

    log_info "control 文件已生成: $control_file"
}

install_files() {
    local deb_root="$1"
    local build_dir="$2"

    log_info "安装文件到 DEB 目录..."

    install -m 755 "${build_dir}/${PROJECT_NAME}" "${deb_root}/usr/bin/${PROJECT_NAME}"
    install -m 644 "${SCRIPT_DIR}/share/applications/${PROJECT_NAME}.desktop" "${deb_root}/usr/share/applications/${PROJECT_NAME}.desktop"
    install -m 644 "${SCRIPT_DIR}/img/icons/hicolor/32x32/apps/${PROJECT_NAME}.png" "${deb_root}/usr/share/icons/hicolor/32x32/apps/${PROJECT_NAME}.png"
    install -m 644 "${SCRIPT_DIR}/img/icons/hicolor/48x48/apps/${PROJECT_NAME}.png" "${deb_root}/usr/share/icons/hicolor/48x48/apps/${PROJECT_NAME}.png"
    install -m 644 "${SCRIPT_DIR}/img/icons/hicolor/128x128/apps/${PROJECT_NAME}.png" "${deb_root}/usr/share/icons/hicolor/128x128/apps/${PROJECT_NAME}.png"
    install -m 644 "${SCRIPT_DIR}/img/icons/hicolor/scalable/apps/${PROJECT_NAME}.svg" "${deb_root}/usr/share/icons/hicolor/scalable/apps/${PROJECT_NAME}.svg"

    log_info "文件安装完成"
}

build_deb() {
    local deb_root="$1"
    local version="$2"
    local output_file="$3"

    log_info "开始构建 DEB 包..."

    if fakeroot dpkg-deb --build --root-owner-group "$deb_root" "$output_file"; then
        log_info "DEB 包构建完成"
        return 0
    else
        log_error "DEB 包构建失败"
        return 1
    fi
}

collect_results() {
    local temp_output="$1"
    local output_dir="$2"

    log_info "收集构建结果..."

    if [ -f "$temp_output" ]; then
        cp "$temp_output" "${output_dir}/"
        local filename=$(basename "$temp_output")
        log_info "已复制: $filename -> ${output_dir}/"
        echo "${output_dir}/${filename}"
        return 0
    else
        log_error "未找到构建好的 DEB 包"
        return 1
    fi
}

cleanup() {
    local keep_temp="$1"

    if [ "$keep_temp" != "true" ]; then
        log_clean "清理临时文件..."
        rm -rf "${TEMP_DIR}"
        log_clean "临时文件已清理"
    else
        log_info "保留临时文件: ${TEMP_DIR}"
    fi
}

show_summary() {
    local output_file="$1"

    echo ""
    echo "=========================================="
    echo "DEB 构建完成！"
    echo "=========================================="
    echo ""
    echo "输出目录: ${OUTPUT_DIR}/"
    echo ""
    echo "生成的 DEB 包:"
    ls -lh "$output_file" 2>/dev/null || echo "  未找到 DEB 包"
    echo ""
    echo "安装命令:"
    echo "  sudo dpkg -i ${output_file}"
    echo "  如果缺少依赖，请运行: sudo apt install -f"
    echo ""
}

main() {
    local version="$DEFAULT_VERSION"
    local keep_temp="false"

    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_help
                exit 0
                ;;
            -v|--version)
                version="$2"
                shift 2
                ;;
            -k|--keep-temp)
                keep_temp="true"
                shift
                ;;
            *)
                log_error "未知选项: $1"
                show_help
                exit 1
                ;;
        esac
    done

    echo "=========================================="
    echo "Ter-Music DEB 构建脚本"
    echo "=========================================="
    echo ""

    check_dependencies

    if [ "$version" = "$DEFAULT_VERSION" ]; then
        version=$(detect_version)
        if [ "$version" != "$DEFAULT_VERSION" ]; then
            log_info "从 Git/defs.h/CMakeLists.txt 检测到版本: $version"
        else
            log_info "无法检测版本，使用默认版本: $version"
        fi
    else
        log_info "使用指定版本: $version"
    fi

    prepare_directories

    local build_dir="${TEMP_DIR}/build"
    local deb_root="${TEMP_DIR}/${PROJECT_NAME}"
    local temp_output="${TEMP_DIR}/${PROJECT_NAME}_${version}_amd64.deb"

    build_from_source "$build_dir"
    create_deb_directory_structure "$deb_root"
    generate_control_file "$deb_root" "$version"
    install_files "$deb_root" "$build_dir"

    if build_deb "$deb_root" "$version" "$temp_output"; then
        output_file=$(collect_results "$temp_output" "$OUTPUT_DIR")
        if [ -n "$output_file" ] && [ -f "$output_file" ]; then
            cleanup "$keep_temp"
            show_summary "$output_file"
        else
            log_error "收集构建结果失败"
            cleanup "$keep_temp"
            exit 1
        fi
    else
        log_error "DEB 构建过程失败"
        cleanup "$keep_temp"
        exit 1
    fi
}

main "$@"
