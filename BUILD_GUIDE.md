# Ter-Music 构建脚本

这个项目提供了多个构建脚本，用于将 Ter-Music 打包成不同的格式。

## 可用的构建脚本

### 1. build-rpm.sh - 构建 RPM 包

将项目构建为标准的 Fedora RPM 包。

**使用方法：**
```bash
# 使用默认版本号构建
./build-rpm.sh

# 指定版本号构建
./build-rpm.sh -v 1.2.3

# 保留临时文件用于调试
./build-rpm.sh --keep-temp

# 显示帮助信息
./build-rpm.sh --help
```

**输出：**
- RPM 包将输出到 `build/rpm/` 目录
- 包括主包、debuginfo 包和 debugsource 包

**安装：**
```bash
sudo dnf install build/rpm/ter-music-*.x86_64.rpm
```

### 2. build-appimage.sh - 构建 AppImage 包

将 RPM 包转换为 AppImage 格式（需要 FUSE 支持）。

**使用方法：**
```bash
# 使用默认 RPM 包构建
./build-appimage.sh

# 指定 RPM 包文件
./build-appimage.sh -r build/rpm/ter-music-1.0.0-1.fc43.x86_64.rpm

# 保留临时文件用于调试
./build-appimage.sh --keep-temp

# 显示帮助信息
./build-appimage.sh --help
```

**输出：**
- AppImage 包将输出到 `build/appimage/` 目录

**使用：**
```bash
# 直接运行
./build/appimage/ter-music-1.0.0-x86_64.AppImage

# 或者先添加执行权限
chmod +x build/appimage/ter-music-1.0.0-x86_64.AppImage
./build/appimage/ter-music-1.0.0-x86_64.AppImage
```

**注意：**
- AppImage 需要 FUSE 支持
- 如果系统缺少 `libfuse.so.2`，AppImage 可能无法直接运行
- 建议使用可移植包作为替代方案

### 3. build-portable.sh - 构建可移植压缩包

将 RPM 包转换为可移植的 tar.gz 压缩包，包含所有必要的依赖库。

**使用方法：**
```bash
# 使用默认 RPM 包构建
./build-portable.sh

# 指定 RPM 包文件
./build-portable.sh -r build/rpm/ter-music-1.0.0-1.fc43.x86_64.rpm

# 保留临时文件用于调试
./build-portable.sh --keep-temp

# 显示帮助信息
./build-portable.sh --help
```

**输出：**
- 可移植包将输出到 `build/portable/` 目录

**使用：**
```bash
# 解压
tar -xzf build/portable/ter-music-1.0.0-portable-x86_64.tar.gz

# 进入目录
cd ter-music-portable

# 运行
./run.sh
```

**优点：**
- 不需要 FUSE 支持
- 包含所有必要的依赖库
- 可以在任何兼容的 Linux 系统上运行
- 不需要安装任何依赖

### 4. build-linyaps.sh - 构建 Linyaps（如意玲珑）包

直接从源码构建 Linyaps（如意玲珑）格式包，适合 deepin 等使用玲珑包管理的系统。

**使用方法：**
```bash
# 使用默认版本号构建
./build-linyaps.sh

# 指定版本号构建
./build-linyaps.sh -v 1.2.3

# 保留临时文件用于调试
./build-linyaps.sh --keep-temp

# 显示帮助信息
./build-linyaps.sh --help
```

**输出：**
- UAB 包和 layer 文件将输出到 `build/linyaps/` 目录

**安装：**
```bash
# 使用 ll-cli 安装
ll-cli install build/linyaps/org.yxzl.ter-music_1.0.0_x86_64.uab

# 运行
ll-cli run org.yxzl.ter-music
```

**优点：**
- 符合 Linyaps 打包规范
- 自动处理依赖关系
- 适合 deepin/UOS 系统用户

## 构建依赖

### build-rpm.sh 依赖：
- `rpm-build`
- `gcc`
- `make`
- `cmake`
- `pkg-config`
- `ffmpeg-free-devel`
- `pulseaudio-libs-devel`
- `ncurses-devel`

### build-appimage.sh 依赖：
- `squashfs-tools`
- `rpm2cpio`
- `cpio`
- `wget` 或 `curl`
- FUSE（用于运行 AppImage）

### build-portable.sh 依赖：
- `rpm2cpio`
- `cpio`
- `tar`

### build-linyaps.sh 依赖：
- `linglong-builder` (ll-builder)
- `cmake`
- `make`
- `pkg-config`
- `libncurses-dev`
- `libavformat-dev`
- `libavcodec-dev`
- `libswresample-dev`
- `libavutil-dev`
- `libtag1-dev`
- `libpulse-dev`

## 推荐的构建流程

1. 首先构建 RPM 包：
   ```bash
   ./build-rpm.sh
   ```

2. 然后根据需要选择转换格式：
   - 如果目标系统支持 FUSE，使用 AppImage：
     ```bash
     ./build-appimage.sh
     ```
   - 如果需要最大的兼容性，使用可移植包：
     ```bash
     ./build-portable.sh
     ```

## 分发建议

- **RPM 包**：适合 Fedora 系统用户，可以通过包管理器安装
- **AppImage**：适合支持 FUSE 的 Linux 系统，单文件分发
- **可移植包**：适合所有 Linux 系统，兼容性最好

## 故障排除

### RPM 包安装失败
如果遇到依赖问题，请确保系统已安装所有必要的开发包：
```bash
sudo dnf install ffmpeg-free-devel pulseaudio-libs-devel ncurses-devel
```

### AppImage 无法运行
如果遇到 FUSE 相关错误，请尝试：
1. 安装 FUSE：`sudo dnf install fuse`
2. 或者使用可移植包作为替代

### 可移植包运行失败
确保解压后的目录结构完整，并且 `run.sh` 脚本有执行权限。