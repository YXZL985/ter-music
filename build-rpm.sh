#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_NAME="ter-music"
DEFAULT_VERSION="1.0.0"
OUTPUT_DIR="${SCRIPT_DIR}/build/rpm"
TEMP_DIR="${SCRIPT_DIR}/.rpmbuild_temp"

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

将 ter-music 项目打包成标准的 Fedora RPM 包

选项:
    -h, --help          显示此帮助信息
    -v, --version VERSION  指定版本号（默认：自动检测）
    -k, --keep-temp     保留临时构建文件（用于调试）

示例:
    $0                  使用自动检测的版本号构建 RPM
    $0 -v 1.2.3         使用指定版本号 1.2.3 构建 RPM
    $0 --keep-temp      构建后保留临时文件

输出:
    RPM 包将输出到: ${OUTPUT_DIR}/

EOF
}

check_dependencies() {
    log_info "检查构建依赖..."
    
    local missing_deps=()
    
    if ! command -v rpmbuild &> /dev/null; then
        missing_deps+=("rpm-build")
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
        echo "  sudo dnf install ${missing_deps[*]}"
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
    
    if [ -f "${SCRIPT_DIR}/CMakeLists.txt" ]; then
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
    mkdir -p "${TEMP_DIR}"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
    
    log_clean "已清理并创建构建目录"
}

generate_spec_file() {
    local version="$1"
    local spec_file="${TEMP_DIR}/SPECS/${PROJECT_NAME}.spec"
    
    log_info "生成 RPM spec file..."
    
    cat > "$spec_file" << EOF
Name:           ${PROJECT_NAME}
Version:        ${version}
Release:        1%{?dist}
Summary:        A terminal-based music player with ncurses interface
License:        GPL-3.0-or-later
URL:            https://gitee.com/yanxi-bamboo-forest/ter-music
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc, make, cmake, pkg-config
BuildRequires:  ffmpeg-free-devel, pulseaudio-libs-devel, ncurses-devel

Requires:       libavcodec-free, libavformat-free, libavutil-free, libswresample-free, pulseaudio-libs, ncurses-libs

%description
Ter-Music is a lightweight terminal-based music player for Linux systems.
It uses FFmpeg for audio decoding, PulseAudio for audio output, and ncursesw
for a beautiful text user interface.

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

%prep
%setup -q

%build
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_bindir}
cd build
install -m 755 %{name} %{buildroot}%{_bindir}/%{name}

%files
%{_bindir}/%{name}

%changelog
* $(LANG=C date +'%a %b %d %Y') Packager <packager@example.com> - ${version}-1
- Initial package
EOF
    
    log_info "Spec 文件已生成: $spec_file"
}

create_source_tarball() {
    local version="$1"
    local tarball_name="${PROJECT_NAME}-${version}.tar.gz"
    local tarball_path="${TEMP_DIR}/SOURCES/${tarball_name}"
    
    log_info "创建源码压缩包..."
    
    if [ -d "${SCRIPT_DIR}/.git" ] && command -v git >/dev/null 2>&1; then
        git -C "${SCRIPT_DIR}" archive --format=tar.gz --prefix="${PROJECT_NAME}-${version}/" HEAD > "$tarball_path"
        if [ $? -eq 0 ]; then
            log_info "源码压缩包已创建: $tarball_path (使用 git archive)"
            return
        fi
        log_info "git archive 失败，回退到手动复制..."
    fi
    
    local source_dir="${TEMP_DIR}/source_package"
    local package_dir="${source_dir}/${PROJECT_NAME}-${version}"
    
    rm -rf "$source_dir"
    mkdir -p "$package_dir"
    
    local files=("src" "include" "CMakeLists.txt" "README.md" "LICENSE")
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
    
    tar -czf "$tarball_path" -C "$source_dir" "${PROJECT_NAME}-${version}"
    
    log_info "源码压缩包已创建: $tarball_path"
}

build_rpm() {
    log_info "开始构建 RPM 包..."
    
    export RPM_TOPDIR="${TEMP_DIR}"
    
    # 执行 rpmbuild 命令并检查退出状态
    if rpmbuild -ba "${TEMP_DIR}/SPECS/${PROJECT_NAME}.spec" \
        --define "_topdir ${TEMP_DIR}" \
        --define "_sourcedir ${TEMP_DIR}/SOURCES" \
        --define "_specdir ${TEMP_DIR}/SPECS" \
        --define "_builddir ${TEMP_DIR}/BUILD" \
        --define "_rpmdir ${TEMP_DIR}/RPMS" \
        --define "_srcrpmdir ${TEMP_DIR}/SRPMS"; then
        log_info "RPM 包构建完成"
        return 0
    else
        log_error "RPM 包构建失败"
        return 1
    fi
}

collect_results() {
    log_info "收集构建结果..."
    
    local found_rpms=0
    local rpm_files=()
    
    # 收集所有 RPM 包
    while IFS= read -r -d '' rpm_file; do
        rpm_files+=("$rpm_file")
    done < <(find "${TEMP_DIR}/RPMS" "${TEMP_DIR}/SRPMS" -name "*.rpm" -type f -print0)
    
    for rpm_file in "${rpm_files[@]}"; do
        cp "$rpm_file" "${OUTPUT_DIR}/"
        local filename=$(basename "$rpm_file")
        log_info "已复制: $filename -> ${OUTPUT_DIR}/"
        ((found_rpms++))
    done
    
    if [ $found_rpms -eq 0 ]; then
        log_error "未找到任何构建好的 RPM 包"
        return 1
    fi
    
    log_info "构建结果已收集到: ${OUTPUT_DIR}/"
    return 0
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
    echo ""
    echo "=========================================="
    echo "RPM 构建完成！"
    echo "=========================================="
    echo ""
    echo "输出目录: ${OUTPUT_DIR}/"
    echo ""
    echo "生成的 RPM 包:"
    ls -lh "${OUTPUT_DIR}"/*.rpm 2>/dev/null || echo "  未找到 RPM 包"
    echo ""
    echo "安装命令:"
    echo "  sudo dnf install ${OUTPUT_DIR}/${PROJECT_NAME}-*.rpm"
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
    echo "Ter-Music RPM 构建脚本"
    echo "=========================================="
    echo ""
    
    check_dependencies
    
    if [ "$version" = "$DEFAULT_VERSION" ]; then
        version=$(detect_version)
        if [ "$version" != "$DEFAULT_VERSION" ]; then
            log_info "从 CMakeLists.txt 检测到版本: $version"
        else
            log_info "无法从 CMakeLists.txt 提取版本，使用默认版本: $version"
        fi
    else
        log_info "使用指定版本: $version"
    fi
    
    prepare_directories
    generate_spec_file "$version"
    if ! create_source_tarball "$version"; then
        log_error "创建源码压缩包失败"
        cleanup "$keep_temp"
        exit 1
    fi
    
    if build_rpm; then
        if collect_results; then
            cleanup "$keep_temp"
            show_summary
        else
            log_error "收集构建结果失败"
            cleanup "$keep_temp"
            exit 1
        fi
    else
        log_error "RPM 构建过程失败"
        cleanup "$keep_temp"
        exit 1
    fi
}

main "$@"
