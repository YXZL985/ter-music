#!/bin/bash

set -e

cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1
SCRIPT_DIR="$(pwd)"
PROJECT_NAME="ter-music"
PROJECT_HOMEPAGE="https://github.com/YXZL985/ter-music"
DEFAULT_VERSION="1.0.0"
OUTPUT_DIR="${SCRIPT_DIR}/build/deb"
TEMP_DIR="${SCRIPT_DIR}/.debbuild_temp"
SOURCE_OUTPUT_DIR="${OUTPUT_DIR}/source"
COLLECTED_RESULT_FILE=""

PROJECT_REQUIRED_PATHS=(
    "src"
    "include"
    "data/applications/${PROJECT_NAME}.desktop"
    "resources/icons/hicolor/32x32/apps/${PROJECT_NAME}.png"
    "resources/icons/hicolor/48x48/apps/${PROJECT_NAME}.png"
    "resources/icons/hicolor/128x128/apps/${PROJECT_NAME}.png"
    "resources/icons/hicolor/scalable/apps/${PROJECT_NAME}.svg"
    "CMakeLists.txt"
    "docs/README.md"
    "docs/translations/README_zh-CN_Modern.md"
    "docs/BUILD_GUIDE.md"
    "LICENSE"
)

PROJECT_SOURCE_PATHS=(
    "src"
    "include"
    "data"
    "resources"
    "cmake"
    "CMakeLists.txt"
    "docs/README.md"
    "docs/translations/README_zh-CN_Modern.md"
    "docs/translations/README_zh-CN_Legacy.md"
    "docs/BUILD_GUIDE.md"
    "LICENSE"
    "scripts/build/build-deb.sh"
    "scripts/build/build-rpm.sh"
    "scripts/build/build-appimage.sh"
    "scripts/build/build-portable.sh"
    "scripts/build/build-linyaps.sh"
)

log_info() {
    echo "[INFO] $1"
}

log_error() {
    echo "[ERROR] $1" >&2
}

log_clean() {
    echo "[CLEAN] $1"
}

log_warn() {
    echo "[WARN] $1" >&2
}

# 复制构建结果到 build/release 目录
copy_to_release() {
    local source_file="$1"
    local release_dir="${SCRIPT_DIR}/build/release"
    
    if [ -f "$source_file" ]; then
        mkdir -p "$release_dir"
        cp "$source_file" "$release_dir/"
        log_info "构建结果已复制到: ${release_dir}/$(basename "$source_file")"
    fi
}

# 检测是否需要交叉编译
is_cross_compiling() {
    local host_arch=$(uname -m)
    local target_arch="$1"
    
    # 标准化架构名称进行比较
    local normalized_host="$host_arch"
    local normalized_target="$target_arch"
    
    # 将 x86_64 和 amd64 视为相同
    if [ "$normalized_host" = "x86_64" ]; then
        normalized_host="amd64"
    fi
    if [ "$normalized_target" = "x86_64" ]; then
        normalized_target="amd64"
    fi
    
    # 将 aarch64 和 arm64 视为相同
    if [ "$normalized_host" = "aarch64" ]; then
        normalized_host="arm64"
    fi
    if [ "$normalized_target" = "aarch64" ]; then
        normalized_target="arm64"
    fi
    
    if [ "$normalized_host" != "$normalized_target" ]; then
        return 0  # true - 需要交叉编译
    else
        return 1  # false - 不需要交叉编译
    fi
}

# 获取交叉编译工具链前缀
get_cross_compile_prefix() {
    local target_arch="$1"
    
    case "$target_arch" in
        arm64|aarch64)
            echo "aarch64-linux-gnu"
            ;;
        *)
            echo ""
            ;;
    esac
}

# 检查交叉编译依赖
check_cross_compile_deps() {
    local target_arch="$1"
    local arch_prefix=$(get_cross_compile_prefix "$target_arch")
    
    if [ -z "$arch_prefix" ]; then
        return 0
    fi
    
    log_info "检查交叉编译工具链..."
    
    local missing_deps=()
    
    # 检查交叉编译器
    if ! command -v ${arch_prefix}-gcc &> /dev/null; then
        missing_deps+=("gcc-${arch_prefix}")
    fi
    
    if ! command -v ${arch_prefix}-g++ &> /dev/null; then
        missing_deps+=("g++-${arch_prefix}")
    fi
    
    if ! command -v ${arch_prefix}-ar &> /dev/null; then
        missing_deps+=("binutils-${arch_prefix}")
    fi
    
    if [ ${#missing_deps[@]} -gt 0 ]; then
        log_error "缺少以下交叉编译工具链组件:"
        for dep in "${missing_deps[@]}"; do
            echo "  - $dep"
        done
        echo ""
        log_error "请使用以下命令安装交叉编译工具链:"
        echo "  sudo apt install gcc-${arch_prefix} g++-${arch_prefix} binutils-${arch_prefix}"
        return 1
    fi
    
    # 检查aarch64开发库
    local pkg_config_dir="/usr/lib/${arch_prefix}/pkgconfig"
    if [ ! -d "$pkg_config_dir" ] && [ ! -d "/usr/${arch_prefix}/lib/pkgconfig" ]; then
        log_warn "未找到 ${arch_prefix} 的 pkgconfig 目录"
        echo ""
        echo "【重要警告】以下命令仅应在以下环境执行："
        echo "  - 临时构建容器/虚拟机"
        echo "  - 专用的交叉编译环境"
        echo "  - 不是你的日常使用系统！"
        echo ""
        log_warn "如需安装目标架构开发库，请谨慎执行:"
        echo "  sudo dpkg --add-architecture arm64"
        echo "  sudo apt update"
        echo "  sudo apt install libncurses-dev:arm64 libavcodec-dev:arm64 libavformat-dev:arm64 \\"
        echo "                   libswresample-dev:arm64 libavutil-dev:arm64 libpulse-dev:arm64"
        echo ""
        echo "【风险提示】这些命令会添加 arm64 架构支持并可能卸载当前系统的"
        echo "            某些 amd64 软件包，可能导致系统不稳定！"
        echo "            建议先在测试环境中尝试，或考虑使用容器进行交叉编译。"
    fi
    
    log_info "交叉编译工具链检查通过"
    return 0
}

# 设置交叉编译环境变量
setup_cross_compile_env() {
    local target_arch="$1"
    local arch_prefix=$(get_cross_compile_prefix "$target_arch")
    
    if [ -z "$arch_prefix" ]; then
        return 0
    fi
    
    log_info "设置交叉编译环境..."
    
    export CC=${arch_prefix}-gcc
    export CXX=${arch_prefix}-g++
    export AR=${arch_prefix}-ar
    export STRIP=${arch_prefix}-strip
    export LD=${arch_prefix}-ld
    export PKG_CONFIG_PATH=/usr/lib/${arch_prefix}/pkgconfig
    export PKG_CONFIG_LIBDIR=/usr/lib/${arch_prefix}/pkgconfig
    
    log_info "交叉编译环境已设置:"
    log_info "  CC=$CC"
    log_info "  CXX=$CXX"
    log_info "  PKG_CONFIG_PATH=$PKG_CONFIG_PATH"
}

show_help() {
    cat << EOF
用法: $0 [选项]

将 ter-music 项目打包成标准的 Debian/Ubuntu DEB 包

选项:
    -h, --help          显示此帮助信息
    -v, --version VERSION  指定版本号（默认：自动检测）
    -a, --arch ARCH     指定目标架构（默认：自动检测）
    -k, --keep-temp     保留临时构建文件（用于调试）
    --with-source       生成源码包（默认不生成）
    --with-debuginfo    生成 debuginfo 包（默认不生成）

支持的架构:
    amd64               Intel/AMD 64位
    arm64               ARM 64位 (aarch64)
    loong64             龙芯新世界
    loongarch64         龙芯旧世界
    sw64                申威
    mips64el            MIPS 64位小端

交叉编译:
    在 x86_64/amd64 机器上构建 arm64 包时，会自动使用交叉编译
    需要安装交叉编译工具链:
      sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu
    需要安装目标架构开发库:
      sudo dpkg --add-architecture arm64
      sudo apt update
      sudo apt install libncurses-dev:arm64 libavcodec-dev:arm64 libavformat-dev:arm64 \
                       libswresample-dev:arm64 libavutil-dev:arm64 libpulse-dev:arm64

示例:
    $0                  使用自动检测的版本号和架构构建 DEB
    $0 -v 1.2.3         使用指定版本号 1.2.3 构建 DEB
    $0 -a arm64         为 ARM 64位架构构建 DEB
    $0 -v 1.2.3 -a loong64  指定版本和架构构建 DEB
    $0 --keep-temp      构建后保留临时文件
    $0 --with-source    同时生成源码包
    $0 --with-debuginfo 同时生成 debuginfo 包

输出:
    DEB 包将输出到: ${OUTPUT_DIR}/<arch>/

EOF
}

check_dependencies() {
    local target_arch="${1:-}"
    
    log_info "检查构建依赖..."

    local missing_deps=()

    if ! command -v dpkg-deb &> /dev/null; then
        missing_deps+=("dpkg-dev")
    fi

    if ! command -v dpkg-source &> /dev/null; then
        missing_deps+=("dpkg-dev")
    fi

    if ! command -v fakeroot &> /dev/null; then
        missing_deps+=("fakeroot")
    fi

    # 检查是否需要交叉编译
    if [ -n "$target_arch" ] && is_cross_compiling "$target_arch"; then
        # 交叉编译模式下，不检查本地gcc
        if ! check_cross_compile_deps "$target_arch"; then
            exit 1
        fi
    else
        # 本地编译模式下，检查本地gcc
        if ! command -v gcc &> /dev/null; then
            missing_deps+=("gcc")
        fi
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

    if ! command -v dh_strip &> /dev/null; then
        missing_deps+=("debhelper")
    fi

    if [ ${#missing_deps[@]} -gt 0 ]; then
        log_error "缺少以下构建工具:"
        # 去重
        local unique_deps=($(echo "${missing_deps[@]}" | tr ' ' '\n' | sort -u | tr '\n' ' '))
        for dep in "${unique_deps[@]}"; do
            echo "  - $dep"
        done
        echo ""
        log_error "请使用以下命令安装缺失的工具:"
        echo "  sudo apt install ${unique_deps[*]}"
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

detect_architecture() {
    local arch=$(uname -m)
    
    case "$arch" in
        x86_64)
            echo "amd64"
            ;;
        aarch64|arm64)
            echo "arm64"
            ;;
        loongarch64)
            echo "loongarch64"
            ;;
        loong64)
            echo "loong64"
            ;;
        mips64)
            echo "mips64el"
            ;;
        sw_64|sw64)
            echo "sw64"
            ;;
        *)
            log_error "未知的架构: $arch"
            echo "unknown"
            return 1
            ;;
    esac
}

validate_architecture() {
    local arch="$1"
    local valid_archs=("amd64" "arm64" "loong64" "loongarch64" "sw64" "mips64el")
    
    for valid_arch in "${valid_archs[@]}"; do
        if [ "$arch" = "$valid_arch" ]; then
            return 0
        fi
    done
    
    log_error "不支持的架构: $arch"
    log_error "支持的架构列表: ${valid_archs[*]}"
    return 1
}

validate_project_layout() {
    local missing_paths=()

    for path in "${PROJECT_REQUIRED_PATHS[@]}"; do
        if [ ! -e "${SCRIPT_DIR}/${path}" ]; then
            missing_paths+=("$path")
        fi
    done

    if [ ${#missing_paths[@]} -gt 0 ]; then
        log_error "项目结构不完整，缺少以下文件或目录:"
        for path in "${missing_paths[@]}"; do
            echo "  - $path"
        done
        return 1
    fi

    return 0
}

copy_project_sources() {
    local destination_dir="$1"

    if [ -d "${SCRIPT_DIR}/.git" ] && command -v git >/dev/null 2>&1; then
        local path=""

        while IFS= read -r -d '' path; do
            mkdir -p "${destination_dir}/$(dirname "${path}")"
            cp -a "${SCRIPT_DIR}/${path}" "${destination_dir}/${path}"
        done < <(git -C "${SCRIPT_DIR}" ls-files -z)

        return 0
    fi

    local path=""
    for path in "${PROJECT_SOURCE_PATHS[@]}"; do
        if [ -e "${SCRIPT_DIR}/${path}" ]; then
            if [ -d "${SCRIPT_DIR}/${path}" ]; then
                mkdir -p "${destination_dir}/${path}"
                cp -a "${SCRIPT_DIR}/${path}/." "${destination_dir}/${path}/"
            else
                mkdir -p "${destination_dir}/$(dirname "${path}")"
                cp -a "${SCRIPT_DIR}/${path}" "${destination_dir}/${path}"
            fi
        fi
    done
}

prepare_directories() {
    local target_arch="$1"
    log_info "准备构建目录..."

    mkdir -p "${OUTPUT_DIR}/${target_arch}"
    mkdir -p "${SOURCE_OUTPUT_DIR}"

    rm -rf "${TEMP_DIR}"
    mkdir -p "${TEMP_DIR}"

    log_clean "已清理并创建构建目录"
}

create_source_tarball() {
    local version="$1"
    local source_dir="${TEMP_DIR}/source"
    local package_dir="${source_dir}/${PROJECT_NAME}-${version}"

    log_info "创建源码压缩包..."

    mkdir -p "$package_dir"
    if ! validate_project_layout; then
        return 1
    fi

    copy_project_sources "$package_dir"

    # 创建原始源码 tar.gz（放在 source 目录的父目录，供 dpkg-source 使用）
    local tarball_path="${TEMP_DIR}/source/${PROJECT_NAME}_${version}.orig.tar.gz"
    tar -czf "$tarball_path" -C "$source_dir" "${PROJECT_NAME}-${version}"

    log_info "源码压缩包已创建: $tarball_path"
    echo "$tarball_path"
}

create_debian_source_package() {
    local version="$1"
    local source_dir="${TEMP_DIR}/source/${PROJECT_NAME}-${version}"

    log_info "创建 Debian 源码包..."

    # 创建 debian 目录
    mkdir -p "${source_dir}/debian"

    # 生成 debian/changelog
    cat > "${source_dir}/debian/changelog" << EOF
${PROJECT_NAME} (${version}-1) unstable; urgency=medium

  * Initial release

 -- Yanxi Bamboo Forest <maintainer@example.com>  $(date -R)

EOF

    # 生成 debian/control
    cat > "${source_dir}/debian/control" << EOF
Source: ${PROJECT_NAME}
Section: sound
Priority: optional
Maintainer: Yanxi Bamboo Forest <maintainer@example.com>
Build-Depends: debhelper (>= 10), cmake, gcc, make, pkg-config,
               libavcodec-dev, libavformat-dev, libavutil-dev, libswresample-dev,
               libpulse-dev, libncursesw5-dev | libncurses-dev
Standards-Version: 4.5.0
Homepage: ${PROJECT_HOMEPAGE}

Package: ${PROJECT_NAME}
Architecture: any
Depends: \${shlibs:Depends}, \${misc:Depends},
         libavcodec60 | libavcodec59 | libavcodec58 | libavcodec-ffmpeg56 | libavcodec-extra,
         libavformat60 | libavformat59 | libavformat58 | libavformat57,
         libavutil58 | libavutil57 | libavutil56 | libavutil55,
         libswresample4 | libswresample3 | libswresample2,
         libpulse0 | libasound2,
         libncursesw6 | libncursesw5
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
EOF

    # 生成 debian/rules
    cat > "${source_dir}/debian/rules" << 'EOF'
#!/usr/bin/make -f

export DH_VERBOSE = 1
export DEB_BUILD_MAINT_OPTIONS = hardening=+all
export DEB_CFLAGS_MAINT_APPEND  = -Wall -pedantic
export DEB_LDFLAGS_MAINT_APPEND = -Wl,--as-needed

%:
	dh $@

override_dh_auto_configure:
	dh_auto_configure --builddirectory=build -- -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr

override_dh_auto_build:
	dh_auto_build --builddirectory=build

override_dh_auto_install:
	dh_auto_install --builddirectory=build --destdir=$(CURDIR)/debian/@PROJECT_NAME@

override_dh_strip:
	dh_strip --dbgsym-migration='@PROJECT_NAME@-dbg (<< @VERSION@-1)'

override_dh_auto_clean:
	dh_auto_clean --builddirectory=build
EOF
    sed -i \
        -e "s|@PROJECT_NAME@|${PROJECT_NAME}|g" \
        -e "s|@VERSION@|${version}|g" \
        "${source_dir}/debian/rules"
    chmod +x "${source_dir}/debian/rules"

    # 生成 debian/copyright
    cat > "${source_dir}/debian/copyright" << EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: ${PROJECT_NAME}
Upstream-Contact: Yanxi Bamboo Forest <maintainer@example.com>
Source: ${PROJECT_HOMEPAGE}

Files: *
Copyright: $(date +%Y) Yanxi Bamboo Forest
License: GPL-3.0-or-later

License: GPL-3.0-or-later
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 .
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 .
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
EOF

    # 生成 debian/source/format
    mkdir -p "${source_dir}/debian/source"
    echo "3.0 (quilt)" > "${source_dir}/debian/source/format"

    # 生成 debian/compat
    echo "12" > "${source_dir}/debian/compat"

    # 使用 dpkg-source 构建源码包
    # 注意：dpkg-source 期望 .orig.tar.gz 在父目录中
    cd "${TEMP_DIR}/source"
    if ! dpkg-source -b "${PROJECT_NAME}-${version}"; then
        log_error "dpkg-source 构建源码包失败"
        return 1
    fi

    # 移动生成的文件到输出目录
    local dsc_file="${TEMP_DIR}/source/${PROJECT_NAME}_${version}-1.dsc"
    local diff_file="${TEMP_DIR}/source/${PROJECT_NAME}_${version}-1.debian.tar.xz"
    local orig_tarball="${TEMP_DIR}/source/${PROJECT_NAME}_${version}.orig.tar.gz"

    if [ -f "$orig_tarball" ]; then
        cp "$orig_tarball" "${SOURCE_OUTPUT_DIR}/"
        log_info "源码包 .orig.tar.gz 已复制: ${SOURCE_OUTPUT_DIR}/$(basename "$orig_tarball")"
    fi

    if [ -f "$dsc_file" ]; then
        cp "$dsc_file" "${SOURCE_OUTPUT_DIR}/"
        log_info "源码包 .dsc 已创建: ${SOURCE_OUTPUT_DIR}/$(basename "$dsc_file")"
    fi

    if [ -f "$diff_file" ]; then
        cp "$diff_file" "${SOURCE_OUTPUT_DIR}/"
        log_info "源码包 debian.tar.xz 已创建: ${SOURCE_OUTPUT_DIR}/$(basename "$diff_file")"
    fi

    log_info "Debian 源码包构建完成"
}

build_from_source() {
    local build_dir="$1"
    local with_debug="$2"
    local target_arch="${3:-}"

    log_info "从源码构建..."
    
    # 检查是否需要交叉编译
    if [ -n "$target_arch" ] && is_cross_compiling "$target_arch"; then
        setup_cross_compile_env "$target_arch"
        log_info "使用交叉编译模式构建目标架构: $target_arch"
    fi

    # 准备CMake参数
    local cmake_args=(-S "${SCRIPT_DIR}" -B "${build_dir}" -DCMAKE_INSTALL_PREFIX=/usr)
    
    if [ "$with_debug" = "true" ]; then
        cmake_args+=(-DCMAKE_BUILD_TYPE=RelWithDebInfo)
    else
        cmake_args+=(-DCMAKE_BUILD_TYPE=Release)
    fi
    
    # 如果是交叉编译，添加工具链文件
    if [ -n "$target_arch" ] && is_cross_compiling "$target_arch"; then
        local toolchain_file="${SCRIPT_DIR}/cmake/toolchain-aarch64-linux-gnu.cmake"
        if [ -f "$toolchain_file" ]; then
            cmake_args+=(-DCMAKE_TOOLCHAIN_FILE="$toolchain_file")
            log_info "使用工具链文件: $toolchain_file"
        fi
    fi
    
    cmake "${cmake_args[@]}"
    cmake --build "${build_dir}" --parallel "$(nproc)"

    log_info "源码构建完成"
}

generate_control_file() {
    local deb_root="$1"
    local version="$2"
    local target_arch="$3"
    local control_file="${deb_root}/DEBIAN/control"

    log_info "生成 DEBIAN/control 文件..."

    cat > "$control_file" << EOF
Package: ${PROJECT_NAME}
Version: ${version}
Architecture: ${target_arch}
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
Depends: libavcodec60 | libavcodec59 | libavcodec58 | libavcodec-ffmpeg56 | libavcodec-extra, libavformat60 | libavformat59 | libavformat58 | libavformat57, libavutil58 | libavutil57 | libavutil56 | libavutil55, libswresample4 | libswresample3 | libswresample2, libpulse0 | libasound2, libncursesw6 | libncursesw5
Section: sound
Priority: optional
Homepage: ${PROJECT_HOMEPAGE}
EOF

    log_info "control 文件已生成: $control_file"
}

install_files() {
    local deb_root="$1"
    local build_dir="$2"

    log_info "安装文件到 DEB 目录..."

    mkdir -p "${deb_root}/DEBIAN"
    DESTDIR="${deb_root}" cmake --build "${build_dir}" --target install

    log_info "文件安装完成"
}

build_deb() {
    local deb_root="$1"
    local version="$2"
    local output_file="$3"

    log_info "开始构建 DEB 包..."

    if fakeroot dpkg-deb --build --root-owner-group "$deb_root" "$output_file"; then
        log_info "DEB 包构建完成"
        copy_to_release "${output_file}"
        return 0
    else
        log_error "DEB 包构建失败"
        return 1
    fi
}

build_debuginfo_deb() {
    local build_dir="$1"
    local version="$2"
    local target_arch="$3"
    local output_file="$4"

    log_info "开始构建 debuginfo DEB 包..."

    local debug_root="${TEMP_DIR}/${PROJECT_NAME}-dbgsym"

    # 创建 debuginfo 目录结构
    rm -rf "$debug_root"
    mkdir -p "${debug_root}/DEBIAN"
    mkdir -p "${debug_root}/usr/lib/debug/.build-id"

    # 生成 control 文件
    cat > "${debug_root}/DEBIAN/control" << EOF
Package: ${PROJECT_NAME}-dbgsym
Source: ${PROJECT_NAME}
Version: ${version}
Auto-Built-Package: debug-symbols
Architecture: ${target_arch}
Section: debug
Priority: optional
Depends: ${PROJECT_NAME} (= ${version})
Description: Debug symbols for ${PROJECT_NAME}
 This package contains the debug symbols for ${PROJECT_NAME}.
EOF

    # 提取 debug 符号
    local binary_path="${build_dir}/${PROJECT_NAME}"
    if command -v objcopy &> /dev/null; then
        # 创建 debug 符号文件
        local debug_file="${debug_root}/usr/lib/debug/${PROJECT_NAME}.debug"
        mkdir -p "$(dirname "$debug_file")"
        objcopy --only-keep-debug "$binary_path" "$debug_file"

        # 获取 build-id
        local build_id=$(readelf -n "$binary_path" 2>/dev/null | grep "Build ID" | awk '{print $3}')
        if [ -n "$build_id" ]; then
            local build_id_prefix="${build_id:0:2}"
            local build_id_suffix="${build_id:2}"
            mkdir -p "${debug_root}/usr/lib/debug/.build-id/${build_id_prefix}"
            ln -sf "/usr/lib/debug/${PROJECT_NAME}.debug" "${debug_root}/usr/lib/debug/.build-id/${build_id_prefix}/${build_id_suffix}"
            ln -sf "/usr/lib/debug/${PROJECT_NAME}.debug" "${debug_root}/usr/lib/debug/.build-id/${build_id_prefix}/${build_id_suffix}.debug"
        fi
    fi

    # 构建 debuginfo deb 包
    if fakeroot dpkg-deb --build --root-owner-group "$debug_root" "$output_file"; then
        log_info "Debuginfo DEB 包构建完成"
        return 0
    else
        log_error "Debuginfo DEB 包构建失败"
        return 1
    fi
}

collect_results() {
    local temp_output="$1"
    local output_dir="$2"
    local file_type="$3"
    local target_arch="$4"

    log_info "收集构建结果..."
    COLLECTED_RESULT_FILE=""

    if [ -f "$temp_output" ]; then
        cp "$temp_output" "${output_dir}/"
        local filename=$(basename "$temp_output")
        log_info "已复制 ${file_type}: $filename -> ${output_dir}/"
        COLLECTED_RESULT_FILE="${output_dir}/${filename}"
        return 0
    else
        log_error "未找到构建好的 ${file_type} 包"
       
        
        # 如果是源码包，输出目录是基础目录
        if [ "$file_type" = "源码" ]; then
            return 1
        fi
        
        # 对于二进制包，检查是否是架构问题
        log_error "请确保目标架构 $target_arch 受支持且交叉编译环境已配置"
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
    local target_arch="$1"
    local output_dir="${OUTPUT_DIR}/${target_arch}"
    shift
    local output_files=("$@")
    local has_source_output="false"

    echo ""
    echo "=========================================="
    echo "DEB 构建完成！"
    echo "=========================================="
    echo ""
    echo "目标架构: $target_arch"
    echo "输出目录: ${output_dir}/"
    for file in "${output_files[@]}"; do
        if [[ "$file" == "${SOURCE_OUTPUT_DIR}/"* ]]; then
            has_source_output="true"
            break
        fi
    done
    if [ "$has_source_output" = "true" ]; then
        echo "源码包目录: ${SOURCE_OUTPUT_DIR}/"
    fi
    echo ""
    echo "生成的包:"
    for file in "${output_files[@]}"; do
        if [ -n "$file" ] && [ -f "$file" ]; then
            ls -lh "$file" 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'
        fi
    done
    echo ""
    echo "安装命令:"
    echo "  sudo dpkg -i ${output_dir}/${PROJECT_NAME}_*_${target_arch}.deb"
    echo "  如果缺少依赖，请运行: sudo apt install -f"
    echo ""
}

main() {
    local version="$DEFAULT_VERSION"
    local keep_temp="false"
    local build_source="false"
    local build_debuginfo="false"
    local target_arch=""

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
            -a|--arch)
                target_arch="$2"
                shift 2
                ;;
            -k|--keep-temp)
                keep_temp="true"
                shift
                ;;
            --no-source)
                build_source="false"
                shift
                ;;
            --with-source)
                build_source="true"
                shift
                ;;
            --no-debuginfo)
                build_debuginfo="false"
                shift
                ;;
            --with-debuginfo)
                build_debuginfo="true"
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

    log_info "构建环境信息:"
    log_info "  操作系统: $(uname -s)"
    log_info "  内核版本: $(uname -r)"
    log_info "  主机架构: $(uname -m)"
    echo ""

    # 首先检测版本和架构
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

    if [ -z "$target_arch" ]; then
        target_arch=$(detect_architecture)
        if [ $? -eq 0 ]; then
            log_info "自动检测到架构: $target_arch"
        else
            log_error "无法检测系统架构"
            exit 1
        fi
    else
        if ! validate_architecture "$target_arch"; then
            exit 1
        fi
        log_info "使用指定架构: $target_arch"
    fi
    
    # 检查依赖（传入目标架构以支持交叉编译检测）
    check_dependencies "$target_arch"
    if ! validate_project_layout; then
        exit 1
    fi

    prepare_directories "$target_arch"

    local output_files=()
    local output_dir="${OUTPUT_DIR}/${target_arch}"

    # 构建源码包
    if [ "$build_source" = "true" ]; then
        log_info "=========================================="
        log_info "开始构建源码包"
        log_info "=========================================="
        create_source_tarball "$version"
        create_debian_source_package "$version"

        # 收集源码包
        local source_outputs=(
            "${SOURCE_OUTPUT_DIR}/${PROJECT_NAME}_${version}.orig.tar.gz"
            "${SOURCE_OUTPUT_DIR}/${PROJECT_NAME}_${version}-1.dsc"
            "${SOURCE_OUTPUT_DIR}/${PROJECT_NAME}_${version}-1.debian.tar.xz"
        )
        for src_file in "${source_outputs[@]}"; do
            if [ -f "$src_file" ]; then
                output_files+=("$src_file")
            fi
        done
    fi

    # 构建二进制包
    log_info "=========================================="
    log_info "开始构建二进制包"
    log_info "=========================================="

    local build_dir="${TEMP_DIR}/build"
    local deb_root="${TEMP_DIR}/${PROJECT_NAME}"
    local temp_output="${TEMP_DIR}/${PROJECT_NAME}_${version}_${target_arch}.deb"

    if [ "$build_debuginfo" = "true" ]; then
        build_from_source "$build_dir" "true" "$target_arch"
    else
        build_from_source "$build_dir" "false" "$target_arch"
    fi

    install_files "$deb_root" "$build_dir"
    generate_control_file "$deb_root" "$version" "$target_arch"

    if build_deb "$deb_root" "$version" "$temp_output"; then
        if collect_results "$temp_output" "$output_dir" "二进制" "$target_arch"; then
            if [ -n "$COLLECTED_RESULT_FILE" ]; then
                output_files+=("$COLLECTED_RESULT_FILE")
            fi
        fi
    else
        log_error "DEB 构建过程失败"
        cleanup "$keep_temp"
        exit 1
    fi

    # 构建 debuginfo 包
    if [ "$build_debuginfo" = "true" ]; then
        log_info "=========================================="
        log_info "开始构建 debuginfo 包"
        log_info "=========================================="

        local debug_output_file="${TEMP_DIR}/${PROJECT_NAME}-dbgsym_${version}_${target_arch}.deb"

        if build_debuginfo_deb "$build_dir" "$version" "$target_arch" "$debug_output_file"; then
            if collect_results "$debug_output_file" "$output_dir" "debuginfo" "$target_arch"; then
                if [ -n "$COLLECTED_RESULT_FILE" ]; then
                    output_files+=("$COLLECTED_RESULT_FILE")
                fi
            fi
        else
            log_error "Debuginfo 包构建失败，继续..."
        fi
    fi

    cleanup "$keep_temp"
    show_summary "$target_arch" "${output_files[@]}"
}

main "$@"
