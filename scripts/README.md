# 构建脚本说明

本目录包含所有用于构建、打包和交叉编译 ter-music 的脚本。

## 目录结构

```
scripts/
├── README.md                    # 本文件
├── build/                      # 构建脚本
│   ├── build-appimage.sh      # 构建 AppImage 包
│   ├── build-deb.sh          # 构建 DEB 包
│   ├── build-linyaps.sh      # 构建 Linyaps (如意玲珑) 包
│   ├── build-portable.sh     # 构建可移植压缩包
│   └── build-rpm.sh         # 构建 RPM 包
└── cross-compile/             # 交叉编译相关
    ├── cross-build.sh        # 交叉编译包装脚本
    ├── Dockerfile           # Docker 镜像定义
    └── docker-compose.yml   # Docker Compose 配置
```

## 打包配置目录

项目根目录下的 `packaging/` 目录包含各平台的打包配置：

```
packaging/
├── debian/        # Debian/Ubuntu 打包配置 (debuild 使用)
└── aur/           # Arch Linux AUR 打包配置 (PKGBUILD, .SRCINFO)
```

## 构建脚本使用方法

### 1. build-appimage.sh - 构建 AppImage 包

将 ter-music 打包成 AppImage 格式，可在大多数 Linux 发行版上运行。

**用法：**
```bash
./scripts/build/build-appimage.sh [选项]
```

**选项：**
- `-v, --version VERSION` - 指定版本号（默认自动检测）
- `-a, --arch ARCH` - 指定目标架构（默认自动检测）
- `-r, --rpm FILE` - 从指定 RPM 包转换
- `-k, --keep-temp` - 保留临时文件
- `-h, --help` - 显示帮助信息

**示例：**
```bash
./scripts/build/build-appimage.sh
./scripts/build/build-appimage.sh -v 1.4.1
./scripts/build/build-appimage.sh -a aarch64
```

### 2. build-deb.sh - 构建 DEB 包

将 ter-music 打包成 Debian/Ubuntu 的 DEB 包。

**用法：**
```bash
./scripts/build/build-deb.sh [选项]
```

**选项：**
- `-v, --version VERSION` - 指定版本号
- `-a, --arch ARCH` - 指定目标架构
- `--with-source` - 生成源码包
- `--with-debuginfo` - 生成 debuginfo 包
- `-k, --keep-temp` - 保留临时文件

**示例：**
```bash
./scripts/build/build-deb.sh
./scripts/build/build-deb.sh -v 1.2.3 -a arm64
./scripts/build/build-deb.sh --with-source
```

### 3. build-linyaps.sh - 构建 Linyaps 包

将 ter-music 打包成如意玲珑 (Linyaps) 格式。

**用法：**
```bash
./scripts/build/build-linyaps.sh [选项]
```

**选项：**
- `-v, --version VERSION` - 指定版本号
- `-a, --arch ARCH` - 指定目标架构
- `-k, --keep-temp` - 保留临时文件

**示例：**
```bash
./scripts/build/build-linyaps.sh
./scripts/build/build-linyaps.sh -v 1.1.2 -a loong64
```

### 4. build-portable.sh - 构建可移植包

创建包含依赖库的可移植压缩包，可在无依赖的 Linux 系统上运行。

**用法：**
```bash
./scripts/build/build-portable.sh [选项]
```

**选项：**
- `-v, --version VERSION` - 指定版本号
- `-a, --arch ARCH` - 指定目标架构
- `-r, --rpm FILE` - 从指定 RPM 包转换
- `-k, --keep-temp` - 保留临时文件

**示例：**
```bash
./scripts/build/build-portable.sh
./scripts/build/build-portable.sh -a aarch64
```

### 5. build-rpm.sh - 构建 RPM 包

将 ter-music 打包成 Fedora/RHEL 的 RPM 包。

**用法：**
```bash
./scripts/build/build-rpm.sh [选项]
```

**选项：**
- `-v, --version VERSION` - 指定版本号
- `-a, --arch ARCH` - 指定目标架构
- `--with-debuginfo` - 生成 debuginfo 包
- `-k, --keep-temp` - 保留临时文件

**示例：**
```bash
./scripts/build/build-rpm.sh
./scripts/build/build-rpm.sh -v 1.2.3 -a loong64
```

## 交叉编译

### cross-build.sh - 交叉编译包装脚本

在 Docker 容器中进行交叉编译。

**用法：**
```bash
./scripts/cross-compile/cross-build.sh [选项] -- [构建脚本参数]
```

**选项：**
- `-b, --build-image` - 重新构建 Docker 镜像
- `-s, --script SCRIPT` - 指定构建脚本（默认 build-deb.sh）
- `-a, --arch ARCH` - 指定目标架构（默认 arm64）
- `-i, --interactive` - 进入交互式 shell
- `--no-cache` - 构建镜像时不使用缓存

**示例：**
```bash
./scripts/cross-compile/cross-build.sh
./scripts/cross-compile/cross-build.sh -a arm64
./scripts/cross-compile/cross-build.sh -s build-rpm.sh
./scripts/cross-compile/cross-build.sh -i
```

### Dockerfile

用于构建交叉编译环境的 Docker 镜像，基于 Ubuntu 22.04，包含：
- 基本构建工具（gcc, cmake, make 等）
- ARM64 交叉编译工具链
- 各种打包工具（dpkg-dev, rpm-build, 等）

### docker-compose.yml

Docker Compose 配置文件，用于快速启动交叉编译环境。

## 支持的架构

所有构建脚本支持以下架构：
- **x86_64** - Intel/AMD 64位
- **aarch64/arm64** - ARM 64位
- **loong64** - 龙芯新世界
- **loongarch64** - 龙芯旧世界
- **sw64** - 申威
- **mips64** - MIPS 64位

## 输出目录

所有构建输出都在 `build/` 目录下：
```
build/
├── appimage/     # AppImage 包
├── deb/          # DEB 包
├── linyaps/      # Linyaps 包
├── portable/     # 可移植包
└── rpm/          # RPM 包
```

## 注意事项

1. 所有脚本都使用相对路径，必须从项目根目录运行
2. 交叉编译需要 Docker 环境
3. 某些包格式可能需要特定的构建依赖
4. 建议使用 `-k, --keep-temp` 选项进行调试

## 依赖检查

各脚本会自动检查所需的构建依赖，如果缺少依赖会给出安装提示。

**常见依赖：**
- cmake, make, gcc
- pkg-config
- 开发库：libavcodec-dev, libavformat-dev, libswresample-dev, libpulse-dev, libncurses-dev
- 打包工具：dpkg-dev, rpm-build, linglong-builder 等
