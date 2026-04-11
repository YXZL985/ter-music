# Ter-Music 构建脚本

这个项目提供了多个构建脚本，用于将 Ter-Music 打包成不同的格式。

## 可用的构建脚本

### 1. build-rpm.sh - 构建 RPM 包
将项目构建为标准的 Fedora RPM 包。
**使用方法：**
```bash
# 使用默认版本号和架构构建
./build-rpm.sh

# 指定版本号构建
./build-rpm.sh -v 1.2.3

# 指定目标架构
./build-rpm.sh -a arm64

# 指定版本号和架构
./build-rpm.sh -v 1.2.3 -a loong64

# 生成 debuginfo 包（默认不生成）
./build-rpm.sh --with-debuginfo

# 保留临时文件用于调试
./build-rpm.sh --keep-temp

# 显示帮助信息
./build-rpm.sh --help
```
**支持的架构：**
- x86_64: Intel/AMD 64位
- arm64: ARM 64位
- loong64: 龙芯新世界
- loongarch64: 龙芯旧世界
- sw64: 申威
- mips64: MIPS 64位

**输出：**
- RPM 包将输出到 `build/rpm/<arch>/` 目录
- 默认只生成主包，使用 `--with-debuginfo` 选项可同时生成 debuginfo 包和 debugsource 包
**安装：**
```bash
sudo dnf install build/rpm/x86_64/ter-music-*.x86_64.rpm
```

### 2. build-appimage.sh - 构建 AppImage 包
直接从源码构建 AppImage 格式（也可从 RPM 转换，需要 FUSE 支持）。
**使用方法：**
```bash
# 自动检测版本和架构，直接从源码构建
./build-appimage.sh

# 指定版本号构建
./build-appimage.sh -v 1.4.1

# 指定目标架构
./build-appimage.sh -a aarch64

# 指定版本和架构
./build-appimage.sh -v 1.4.1 -a aarch64

# 从指定 RPM 包文件转换
./build-appimage.sh -r build/rpm/x86_64/ter-music-1.0.0-1.x86_64.rpm

# 保留临时文件用于调试
./build-appimage.sh --keep-temp

# 显示帮助信息
./build-appimage.sh --help
```
**支持的架构：**
- x86_64: Intel/AMD 64位
- aarch64: ARM 64位
- loong64: 龙芯（包括新世界和旧世界）
- loongarch64: 龙芯旧世界
- sw64: 申威
- mips64: MIPS 64位

**输出：**
- AppImage 包将输出到: `build/appimage/<arch>/` 目录

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
直接从源码构建可移植的 tar.gz 压缩包，包含所有必要的依赖库（也可从 RPM 转换）。
**使用方法：**
```bash
# 自动检测版本和架构，直接从源码构建
./build-portable.sh

# 指定版本号构建
./build-portable.sh -v 1.4.1

# 指定目标架构
./build-portable.sh -a aarch64

# 指定版本和架构
./build-portable.sh -v 1.4.1 -a aarch64

# 从指定 RPM 包文件转换
./build-portable.sh -r build/rpm/x86_64/ter-music-1.0.0-1.x86_64.rpm

# 保留临时文件用于调试
./build-portable.sh --keep-temp

# 显示帮助信息
./build-portable.sh --help
```
**支持的架构：**
- x86_64: Intel/AMD 64位
- aarch64: ARM 64位
- loong64: 龙芯（包括新世界和旧世界）
- loongarch64: 龙芯旧世界
- sw64: 申威
- mips64: MIPS 64位

**输出：**
- 可移植包将输出到: `build/portable/<arch>/` 目录

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
# 使用默认版本号和架构构建
./build-linyaps.sh

#指定版本号构建
./build-linyaps.sh -v 1.2.3

# 指定目标架构
./build-linyaps.sh -a arm64

# 指定版本和架构
./build-linyaps.sh -v 1.2.3 -a loong64

# 保留临时文件用于调试
./build-linyaps.sh --keep-temp

# 显示帮助信息
./build-linyaps.sh --help
```
**支持的架构：**
- x86_64: Intel/AMD 64位
- arm64: ARM 64位
- loong64: 龙芯（包括新世界和旧世界）
- mips64: MIPS 64位
- sw64: 申威

**输出：**
- UAB 包和 layer 文件将输出到: `build/linyaps/<arch>/` 目录

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

### 5. build-deb.sh - 构建 DEB 包
将项目构建为标准的 Debian/Ubuntu DEB 包，适合 Debian、Ubuntu、Linux Mint、deepin 等基于 Debian 的发行版。
**使用方法：**
```bash
# 使用自动检测的版本号和架构构建 DEB
./build-deb.sh

# 指定版本号构建
./build-deb.sh -v 1.4.1

# 指定目标架构
./build-deb.sh -a arm64

# 指定版本号和架构
./build-deb.sh -v 1.4.1 -a arm64

# 保留临时文件用于调试
./build-deb.sh --keep-temp

# 显示帮助信息
./build-deb.sh --help
```
**支持的架构：**
- amd64: Intel/AMD 64位
- arm64: ARM 64位
- loong64: 龙芯新世界
- loongarch64: 龙芯旧世界
- sw64: 申威
- mips64el: MIPS 64位小端

**输出：**
- DEB 包将输出到: `build/deb/<arch>/` 目录

**安装：**
```bash
sudo dpkg -i build/deb/ter-music_*_amd64.deb
# 如果缺少依赖，请运行：
sudo apt install -f
```

**优点：**
- 标准 DEB 格式，适合 Debian/Ubuntu 系发行版
- 自动处理依赖关系
- 可通过 `apt` 工具管理安装和卸载

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
- `cmake`
- `make`
- `gcc`
- `wget` 或 `curl`
- FUSE（用于运行 AppImage）
- `rpm2cpio` 和 `cpio`（仅当从 RPM 转换时需要）

### build-portable.sh 依赖：
- `cmake`
- `make`
- `gcc`
- `tar`
- `rpm2cpio` 和 `cpio`（仅当从 RPM 转换时需要）

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

### build-deb.sh 依赖：
- `dpkg-dev`
- `fakeroot`
- `cmake`
- `make`
- `gcc`
- `tar`

在 Debian/Ubuntu 上安装构建依赖：
```bash
sudo apt install dpkg-dev fakeroot cmake make gcc
```

## 推荐的构建流程

现在可以直接构建可移植包或 AppImage，不需要先构建 RPM。推荐直接指定版本号构建：

- 直接指定版本号构建可移植包（推荐，兼容性最好）：
  ```bash
  ./build-portable.sh -v 1.4.1
  ```

- 直接指定版本号构建 AppImage（推荐）：
  ```bash
  ./build-appimage.sh -v 1.4.1
  ```

- 自动检测版本直接构建：
  ```bash
  ./build-portable.sh
  ./build-appimage.sh
  ```

- 如果需要构建 RPM 包，可以先构建 RPM 再转换：
  ```bash
  ./build-rpm.sh -v 1.4.1
  ./build-portable.sh -r build/rpm/ter-music-*.rpm
  ```

## 分发建议

- **RPM 包**：适合 Fedora/RHEL 系统用户，可以通过包管理器安装
- **DEB 包**：适合 Debian/Ubuntu 系发行版（Ubuntu、Linux Mint、deepin 等），可以通过 dpkg/apt 安装
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