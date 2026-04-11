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
    --no-source         不生成源码包
    --no-debuginfo      不生成 debuginfo 包

示例:
    $0                  使用自动检测的版本号构建 DEB
    $0 -v 1.2.3         使用指定版本号 1.2.3 构建 DEB
    $0 --keep-temp      构建后保留临时文件
    $0 --no-source      只构建二进制包，不生成源码包

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

    if ! command -v dpkg-source &> /dev/null; then
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

prepare_directories() {
    log_info "准备构建目录..."

    mkdir -p "${OUTPUT_DIR}"

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

    local files=("src" "include" "share" "img" "CMakeLists.txt" "README.md" "LICENSE" "README_CN.md" "BUILD_GUIDE.md")
    local missing_files=()

    for file in "${files[@]}"; do
        if [ -e "${SCRIPT_DIR}/${file}" ]; then
            cp -r "${SCRIPT_DIR}/${file}" "$package_dir/"
        else
            missing_files+=("$file")
        fi
    done

    if [ ${#missing_files[@]} -gt 0 ]; then
        log_error "缺少必要的源文件: ${missing_files[*]}"
        return 1
    fi

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
Homepage: https://gitee.com/yanxi-bamboo-forest/ter-music

Package: ${PROJECT_NAME}
Architecture: amd64
Depends: \${shlibs:Depends}, \${misc:Depends},
         libavcodec58 | libavcodec-ffmpeg56 | libavcodec-extra,
         libavformat58 | libavformat57,
         libavutil56 | libavutil55,
         libswresample3 | libswresample2,
         libpulse0 | libasound2,
         libncursesw5 | libncursesw6
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

Package: ${PROJECT_NAME}-dbgsym
Architecture: amd64
Section: debug
Priority: optional
Depends: ${PROJECT_NAME} (= \${binary:Version})
Description: Debug symbols for ${PROJECT_NAME}
 This package contains the debug symbols for ${PROJECT_NAME}.
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
	mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr

override_dh_auto_build:
	cd build && $(MAKE)

override_dh_auto_install:
	cd build && $(MAKE) DESTDIR=$(CURDIR)/debian/${PROJECT_NAME} install
	# 手动安装文件
	install -D -m 755 build/${PROJECT_NAME} debian/${PROJECT_NAME}/usr/bin/${PROJECT_NAME}
	install -D -m 644 share/applications/${PROJECT_NAME}.desktop debian/${PROJECT_NAME}/usr/share/applications/${PROJECT_NAME}.desktop
	install -D -m 644 img/icons/hicolor/32x32/apps/${PROJECT_NAME}.png debian/${PROJECT_NAME}/usr/share/icons/hicolor/32x32/apps/${PROJECT_NAME}.png
	install -D -m 644 img/icons/hicolor/48x48/apps/${PROJECT_NAME}.png debian/${PROJECT_NAME}/usr/share/icons/hicolor/48x48/apps/${PROJECT_NAME}.png
	install -D -m 644 img/icons/hicolor/128x128/apps/${PROJECT_NAME}.png debian/${PROJECT_NAME}/usr/share/icons/hicolor/128x128/apps/${PROJECT_NAME}.png
	install -D -m 644 img/icons/hicolor/scalable/apps/${PROJECT_NAME}.svg debian/${PROJECT_NAME}/usr/share/icons/hicolor/scalable/apps/${PROJECT_NAME}.svg

override_dh_strip:
	dh_strip --dbgsym-migration='${PROJECT_NAME}-dbg (<< ${version}-1)'

override_dh_auto_clean:
	rm -rf build
EOF
    chmod +x "${source_dir}/debian/rules"

    # 生成 debian/copyright
    cat > "${source_dir}/debian/copyright" << EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: ${PROJECT_NAME}
Upstream-Contact: Yanxi Bamboo Forest <maintainer@example.com>
Source: https://gitee.com/yanxi-bamboo-forest/ter-music

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
    dpkg-source -b "${PROJECT_NAME}-${version}"

    # 移动生成的文件到输出目录
    local dsc_file="${TEMP_DIR}/source/${PROJECT_NAME}_${version}-1.dsc"
    local diff_file="${TEMP_DIR}/source/${PROJECT_NAME}_${version}-1.debian.tar.xz"
    local orig_tarball="${TEMP_DIR}/source/${PROJECT_NAME}_${version}.orig.tar.gz"

    if [ -f "$orig_tarball" ]; then
        cp "$orig_tarball" "${OUTPUT_DIR}/"
        log_info "源码包 .orig.tar.gz 已复制: ${OUTPUT_DIR}/$(basename "$orig_tarball")"
    fi

    if [ -f "$dsc_file" ]; then
        cp "$dsc_file" "${OUTPUT_DIR}/"
        log_info "源码包 .dsc 已创建: ${OUTPUT_DIR}/$(basename "$dsc_file")"
    fi

    if [ -f "$diff_file" ]; then
        cp "$diff_file" "${OUTPUT_DIR}/"
        log_info "源码包 debian.tar.xz 已创建: ${OUTPUT_DIR}/$(basename "$diff_file")"
    fi

    log_info "Debian 源码包构建完成"
}

build_from_source() {
    local build_dir="$1"
    local with_debug="$2"

    log_info "从源码构建..."

    mkdir -p "${build_dir}"
    cd "${build_dir}"

    if [ "$with_debug" = "true" ]; then
        cmake "${SCRIPT_DIR}" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr
    else
        cmake "${SCRIPT_DIR}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
    fi
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

build_debuginfo_deb() {
    local build_dir="$1"
    local version="$2"
    local output_file="$3"

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
Architecture: amd64
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

    log_info "收集构建结果..."

    if [ -f "$temp_output" ]; then
        cp "$temp_output" "${output_dir}/"
        local filename=$(basename "$temp_output")
        log_info "已复制 ${file_type}: $filename -> ${output_dir}/"
        echo "${output_dir}/${filename}"
        return 0
    else
        log_error "未找到构建好的 ${file_type} 包"
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
    local output_files=("$@")

    echo ""
    echo "=========================================="
    echo "DEB 构建完成！"
    echo "=========================================="
    echo ""
    echo "输出目录: ${OUTPUT_DIR}/"
    echo ""
    echo "生成的包:"
    for file in "${output_files[@]}"; do
        if [ -n "$file" ] && [ -f "$file" ]; then
            ls -lh "$file" 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'
        fi
    done
    echo ""
    echo "安装命令:"
    echo "  sudo dpkg -i ${OUTPUT_DIR}/${PROJECT_NAME}_*_amd64.deb"
    echo "  如果缺少依赖，请运行: sudo apt install -f"
    echo ""
}

main() {
    local version="$DEFAULT_VERSION"
    local keep_temp="false"
    local build_source="true"
    local build_debuginfo="true"

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
            --no-source)
                build_source="false"
                shift
                ;;
            --no-debuginfo)
                build_debuginfo="false"
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

    local output_files=()

    # 构建源码包
    if [ "$build_source" = "true" ]; then
        log_info "=========================================="
        log_info "开始构建源码包"
        log_info "=========================================="
        create_source_tarball "$version"
        create_debian_source_package "$version"

        # 收集源码包
        for ext in .orig.tar.gz .dsc .debian.tar.xz; do
            local src_file="${OUTPUT_DIR}/${PROJECT_NAME}_${version}${ext}"
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
    local temp_output="${TEMP_DIR}/${PROJECT_NAME}_${version}_amd64.deb"

    if [ "$build_debuginfo" = "true" ]; then
        build_from_source "$build_dir" "true"
    else
        build_from_source "$build_dir" "false"
    fi

    create_deb_directory_structure "$deb_root"
    generate_control_file "$deb_root" "$version"
    install_files "$deb_root" "$build_dir"

    if build_deb "$deb_root" "$version" "$temp_output"; then
        local bin_output
        bin_output=$(collect_results "$temp_output" "$OUTPUT_DIR" "二进制")
        if [ -n "$bin_output" ]; then
            output_files+=("$bin_output")
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

        local debug_output_file="${TEMP_DIR}/${PROJECT_NAME}-dbgsym_${version}_amd64.deb"

        if build_debuginfo_deb "$build_dir" "$version" "$debug_output_file"; then
            local dbg_output
            dbg_output=$(collect_results "$debug_output_file" "$OUTPUT_DIR" "debuginfo")
            if [ -n "$dbg_output" ]; then
                output_files+=("$dbg_output")
            fi
        else
            log_error "Debuginfo 包构建失败，继续..."
        fi
    fi

    cleanup "$keep_temp"
    show_summary "${output_files[@]}"
}

main "$@"
