# Ter-Music - 终端音乐播放器

![License: GPLv3](https://img.shields.io/badge/License-GPLv3-blue.svg)
![Language: C](https://img.shields.io/badge/Language-C-blue.svg)
![Platform: Linux](https://img.shields.io/badge/Platform-Linux-green.svg)

## 1. 项目介绍

### 1.1 核心功能

Ter-Music 是一个轻量级、基于终端的命令行音乐播放器，专为 Linux 系统设计。它利用 FFmpeg 进行音频解码，PulseAudio 进行音频输出，并通过 ncursesw 提供美观的文本用户界面。

**主要功能包括：**

- 🎵 **支持多种音频格式**：MP3、WAV、FLAC、OGG、M4A、AAC、WMA、APE、OPUS 等主流格式
- 📝 **LRC 歌词同步显示**：自动加载并同步显示歌词，跟随播放进度高亮显示
- 🎶 **多种循环播放模式**：顺序播放、单曲循环、列表循环、随机播放
- 📚 **播放列表管理**：支持用户自定义创建多个播放列表
- ❤️ **收藏夹功能**：收藏喜爱的歌曲，快速访问
- 🕒 **播放历史记录**：自动记录播放历史，方便回顾
- 📂 **目录历史**：记录最近访问的音乐目录
- ⚙️ **可配置主题颜色**：支持自定义界面颜色主题
- ⌨️ **键盘快捷键操作**：全键盘操作，高效便捷
- 📊 **实时进度条**：流畅的播放进度显示和拖拽跳转

### 1.2 设计理念

Ter-Music 遵循**简约、高效、原生**的设计理念：

- **轻量级**：不依赖重型桌面环境，资源占用极低
- **终端原生**：完全基于文本用户界面，适合服务器、嵌入式设备和喜欢终端工作流的用户
- **模块化设计**：清晰的模块划分，易于维护和扩展
- **Unix 哲学**：做好一件事，与其他工具良好协作
- **零跟踪**：不收集任何用户数据，尊重隐私

### 1.3 主要特点

| 特性                                         | 说明                       |
| ------------------------------------------ | ------------------------ |
| 🚀 **低资源占用**                               | 内存占用通常 < 10MB，CPU 使用率极低  |
| 🎨 **美观的 TUI**                             | 分栏布局，彩色界面，支持终端大小自适应      |
| 🌍 **UTF-8 中文支持**                          | 完美支持 UTF-8 编码，正确显示中文歌曲信息 |
| 🔄 **持久化存储**                               | 配置、收藏夹、播放历史自动保存，程序重启后恢复  |
| 🎯 **多视图切换**：通过 F2-F7 功能键快速切换设置、历史、播放列表等视图 | <br />                   |
| ⚡ **响应式 UI**：100 FPS 刷新率，进度条流畅更新           | <br />                   |
| 🔧 **CMake 构建**：现代化构建系统，跨平台兼容性好            | <br />                   |
| 🔊 **PulseAudio 音频后端**：稳定的低延迟音频输出          | <br />                   |

### 1.4 应用场景

- **服务器/无头系统**：在没有图形界面的服务器上播放音乐
- **嵌入式设备**：在资源有限的嵌入式 Linux 设备上运行
- **开发人员**：在终端工作时边编码边听音乐，无需切换窗口
- **极简主义者**：喜欢简约软件，不需要复杂图形界面的用户
- **学习参考**：学习 C 语言、FFmpeg、ncurses 编程的优秀示例项目

### 1.5 目标用户群体

- Linux 高级用户和命令行爱好者
- 嵌入式开发人员和系统管理员
- 追求极简主义的用户
- 需要在无图形界面环境下播放音乐的用户
- 学习 C 语言和多媒体编程的开发者

## 2. 编译环境要求

### 2.1 操作系统

- **支持系统**：Linux 内核 3.10 或更高版本
- **推荐发行版**：Fedora 30+, Ubuntu 20.04+, Arch Linux 最新版
- **不支持**：Windows、macOS（欢迎贡献移植）

### 2.2 硬件配置

| 组件      | 最低要求            | 推荐配置            |
| ------- | --------------- | --------------- |
| **CPU** | 单核 1GHz         | 双核 2GHz 或更高     |
| **内存**  | 64MB 可用内存       | 128MB 可用内存或更高   |
| **存储**  | 5MB 可用磁盘空间      | 10MB 可用磁盘空间     |
| **声卡**  | PulseAudio 服务运行 | PulseAudio 服务运行 |

### 2.3 编译器版本

- **GCC**：GCC 7.0 或更高版本
- **Clang**：Clang 6.0 或更高版本
- **C 标准**：C99 或更高

### 2.4 构建工具

- **CMake**：3.10 或更高版本
- **Make**：GNU Make 4.0 或更高版本
- **pkg-config**：0.29 或更高版本

## 3. 依赖项及安装命令

### 3.1 必需依赖包

| 依赖库               | 版本要求  | 用途                                                      |
| ----------------- | ----- | ------------------------------------------------------- |
| `ffmpeg-libs`     | 4.0+  | 音频解码（libavcodec, libavformat, libswresample, libavutil） |
| `pulseaudio-libs` | 10.0+ | PulseAudio 音频输出                                         |
| `ncursesw`        | 6.0+  | 文本用户界面，宽字符支持                                            |
| `pthread`         | 系统自带  | 多线程支持                                                   |
| `cmake`           | 3.10+ | 构建系统（编译时需要）                                             |
| `gcc`             | 7.0+  | C 编译器（编译时需要）                                            |
| `make`            | -     | 构建工具（编译时需要）                                             |
| `pkg-config`      | -     | 依赖检测（编译时需要）                                             |

### 3.2 Fedora / RHEL / CentOS

```bash
sudo dnf install cmake gcc make pkg-config
sudo dnf install ffmpeg-free-devel pulseaudio-libs-devel ncurses-devel
```

### 3.3 Ubuntu / Debian / Linux Mint

```bash
sudo apt update
sudo apt install cmake gcc make pkg-config
sudo apt install libavcodec-dev libavformat-dev libswresample-dev libavutil-dev
sudo apt install libpulse-dev libncursesw5-dev
```

**注意**：如果找不到 ffmpeg 开发包，您可能需要先启用 universe 仓库：

```bash
sudo add-apt-repository universe
sudo apt update
```

### 3.4 Arch Linux

```bash
sudo pacman -S cmake gcc make pkg-config
sudo pacman -S ffmpeg pulseaudio ncurses
```

## 4. 编译步骤

### 4.1 获取源代码

```bash
git clone https://gitee.com/yanxi-bamboo-forest/ter-music.git
cd ter-music
```

### 4.2 创建构建目录

```bash
mkdir build
cd build
```

### 4.3 配置 CMake

```bash
cmake ..
```

CMake 会自动检测系统中的所有依赖库。如果缺少任何依赖，会显示明确的错误信息。

**可选 CMake 参数：**

```bash
# 自定义安装前缀（默认：/usr/local）
cmake .. -DCMAKE_INSTALL_PREFIX=/usr

# 启用调试编译
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 启用编译优化
cmake .. -DCMAKE_BUILD_TYPE=Release
```

### 4.4 编译

```bash
make -j$(nproc)
```

`-j$(nproc)` 会使用所有可用 CPU 核心并行编译，加快编译速度。

### 4.5 安装（可选）

```bash
sudo make install
```

安装后，您可以直接在终端输入 `ter-music` 启动程序。

### 4.6 卸载（如果已安装）

```bash
cd build
sudo make uninstall
```

### 4.7 清理构建文件

```bash
cd build
make clean
# 或者完全删除构建目录
rm -rf build
```

### 4.8 常见编译问题

**问题 1：找不到 PulseAudio 库**

```
解决：安装 pulseaudio-libs-devel（Fedora）或 libpulse-dev（Ubuntu）
```

**问题 2：找不到 ncursesw 库**

```
解决：安装 ncurses-devel（Fedora）或 libncursesw5-dev（Ubuntu）
```

**问题 3：找不到 ffmpeg 头文件**

```
解决：安装 ffmpeg-devel（Fedora）或 libavcodec-dev libavformat-dev...（Ubuntu）
```

### 4.9 构建脚本使用方法

Ter-Music 提供了两个构建脚本，用于创建不同格式的可执行文件：

#### 4.9.1 AppImage 构建脚本

`build-appimage.sh` 脚本用于将 RPM 包转换为 AppImage 格式，适用于各种 Linux 发行版：

**使用方法：**

```bash
# 使用默认 RPM 包构建 AppImage
./build-appimage.sh

# 使用指定的 RPM 包构建 AppImage
./build-appimage.sh -r build/rpm/ter-music-1.0.0-1.fc43.x86_64.rpm

# 构建后保留临时文件（用于调试）
./build-appimage.sh --keep-temp
```

**输出：**

- AppImage 包将输出到：`build/appimage/`

#### 4.9.2 可移植包构建脚本

`build-portable.sh` 脚本用于将 RPM 包转换为可移植的压缩包格式，包含所有必要的依赖库：

**使用方法：**

```bash
# 使用默认 RPM 包构建可移植包
./build-portable.sh

# 使用指定的 RPM 包构建可移植包
./build-portable.sh -r build/rpm/ter-music-1.0.0-1.fc43.x86_64.rpm

# 构建后保留临时文件（用于调试）
./build-portable.sh --keep-temp
```

**输出：**

- 可移植包将输出到：`build/portable/`

**注意：** 这两个脚本都需要先运行 `build-rpm.sh` 生成 RPM 包，或者使用 `-r` 选项指定已有的 RPM 包路径。

#### 4.9.3 Linyaps（如意玲珑）构建脚本

`build-linyaps.sh` 脚本直接从源码构建 Linyaps（如意玲珑）格式包，适合 deepin/UOS 等使用玲珑包管理的系统：

**使用方法：**

```bash
# 使用自动检测版本构建 Linyaps 包
./build-linyaps.sh

# 指定版本号构建
./build-linyaps.sh -v 1.2.3

# 构建后保留临时文件（用于调试）
./build-linyaps.sh --keep-temp

# 显示帮助信息
./build-linyaps.sh --help
```

**输出：**

- UAB 包和 layer 文件将输出到：`build/linyaps/`

**安装运行：**

```bash
# 使用 ll-cli 安装
ll-cli install build/linyaps/org.yxzl.ter-music_1.0.0_x86_64.uab

# 运行
ll-cli run org.yxzl.ter-music
```

**构建依赖（Debian/Ubuntu/deepin）：**

```bash
sudo apt install linglong-builder cmake make pkg-config
sudo apt install libncurses-dev libavformat-dev libavcodec-dev libswresample-dev
sudo apt install libavutil-dev libtag1-dev libpulse-dev
```

## 5. 使用方法

### 5.1 启动程序

**如果已安装：**

```bash
ter-music
```

**如果未安装，直接从构建目录运行：**

```bash
cd build
./ter-music
```

### 5.2 命令行参数

```bash
ter-music [OPTIONS]

选项：
  -o, --open <path>    启动时直接打开指定音乐目录
  -h, --help           显示帮助信息
```

**示例：**

```bash
# 启动时打开我的音乐文件夹
ter-music -o ~/Music

# 显示帮助
ter-music --help
```

### 5.3 界面布局

启动后，您将看到三栏布局：

```
┌───────────────────────────┬───────────────┐
│  Play List                │  Lyrics      │
│                           │               │
│  歌曲列表区域             │ 歌词显示区域  │
│                           │               │
│                           │               │
├───────────────────────────┤               │
│  Controls                 │               │
│  [<<] [Play/Pause] [>>]  │               │
│  [Stop] [Loop:Off]        │               │
└───────────────────────────┴───────────────┘
```

- **左侧上方**：播放列表，显示当前目录下的所有音频文件
- **左侧下方**：控制栏，包含播放控制按钮和进度条
- **右侧**：歌词显示区域，同步显示当前播放歌曲的歌词

### 5.4 基本操作

#### 焦点切换

| 按键  | 功能       |
| --- | -------- |
| `C` | 切换到控制区焦点 |
| `L` | 切换到列表区焦点 |
| `Y` | 切换到歌词区焦点 |

#### 列表区操作（焦点在播放列表）

| 按键                | 功能                 |
| ----------------- | ------------------ |
| `↑` / `↓`         | 上下选择歌曲             |
| `Space` / `Enter` | 播放选中歌曲             |
| `O` / `o`         | 打开新的音乐文件夹          |
| `F` / `f`         | 将选中歌曲添加到收藏夹        |
| `A` / `a`         | 将选中歌曲添加到第一个自定义播放列表 |
| `F1`              | 返回主界面               |
| `F2`              | 打开设置视图             |
| `F3`              | 打开播放历史视图           |
| `F4`              | 打开播放列表管理视图         |
| `F5`              | 打开收藏夹视图            |
| `F6`              | 打开关于信息视图           |
| `F7`              | 退出程序               |
| `q`               | 退出程序               |

#### 控制区操作（焦点在控制栏）

| 按键        | 功能              |
| --------- | --------------- |
| `←` / `→` | 左右选择控制按钮        |
| `Space`   | 激活当前选中的按钮       |
| `,`（逗号）   | 后退 5 秒          |
| `.`（句号）   | 前进 5 秒          |
| `D` / `d` | 跳转到当前歌词行对应的播放位置 |

**控制按钮说明：**

| 按钮           | 功能                                   |
| ------------ | ------------------------------------ |
| `<<`         | 上一曲                                  |
| `Play/Pause` | 播放/暂停                                |
| `>>`         | 下一曲                                  |
| `Stop`       | 停止播放                                 |
| `Loop`       | 切换循环模式（Off → Single → List → Random） |
| `Progress`   | 进度条（显示当前播放进度）                        |

#### 歌词区操作（焦点在歌词区域）

| 按键                            | 功能              |
| ----------------------------- | --------------- |
| `↑` / `↓`                     | 上下选择歌词行         |
| `D` / `d` / `Enter` / `Space` | 跳转到选中歌词行对应的播放位置 |

#### 功能键（全局可用）

| 按键    | 功能           |
| ----- | ------------ |
| `F1`  | 返回主界面        |
| `F2`  | 切换到设置视图      |
| `F3`  | 切换到播放历史视图    |
| `F4`  | 切换到播放列表管理视图  |
| `F5`  | 切换到收藏夹视图     |
| `F6`  | 切换到关于信息视图    |
| `F7`  | 退出程序         |
| `Esc` | 退出当前视图，返回主界面 |

### 5.5 循环模式说明

| 模式       | 说明              |
| -------- | --------------- |
| `Off`    | 顺序播放，播放到列表末尾停止  |
| `Single` | 单曲循环，重复播放当前歌曲   |
| `List`   | 列表循环，播放完一轮后从头开始 |
| `Random` | 随机播放，随机选择下一首歌曲  |

按 `Space` 激活 Loop 按钮可循环切换模式。

### 5.6 歌词显示

Ter-Music 支持自动加载 LRC 格式歌词文件：

- 歌词文件应与音频文件放在同一目录
- 歌词文件名应与音频文件名相同，扩展名为 `.lrc`
- 例如：`song.mp3` → `song.lrc`
- 程序会根据播放时间自动高亮当前歌词
- 如果找不到歌词文件，歌词区域会显示 "No lyrics loaded"

### 5.7 配置文件

配置文件存储在 `~/.config/ter-music/config`。程序会在首次运行时自动创建。

**配置项包括：**

- `default_startup_path`：默认启动目录
- `auto_play_on_start`：启动时自动播放（0/1）
- `remember_last_path`：记住上次打开的目录（0/1）
- 颜色主题配置：所有界面元素的前景色和背景色

程序会自动保存配置，修改设置后立即生效。

### 5.8 数据存储位置

所有用户数据都存储在 `~/.config/ter-music/` 目录下：

```
~/.config/ter-music/
├── config          # 配置文件
├── history        # 播放历史
├── favorites      # 收藏夹
├── dir_history    # 目录访问历史
└── playlists/     # 自定义播放列表
```

### 5.9 基本使用流程

**示例：第一次使用**

1. 启动程序：
   ```bash
   ter-music
   ```
2. 按 `O` 打开文件夹，输入您的音乐目录路径，例如：
   ```
   /home/yourname/Music
   ```
3. 程序会扫描目录中的所有音频文件并显示在播放列表中
4. 使用 `↑` `↓` 选择您想听的歌曲，按 `Space` 开始播放
5. 如果有歌词文件，歌词会自动加载并在右侧同步显示
6. 使用 `,` 和 `.` 可以快退/快进 5 秒

**示例：添加歌曲到收藏夹**

1. 在列表区选中想听的歌曲
2. 按 `F`，底部状态栏会显示 "Added to favorites!"
3. 按 `F4` 可以查看所有收藏的歌曲
4. 在收藏夹视图中可以选择并播放收藏的歌曲

**示例：创建自定义播放列表**

1. 按 `F3` 进入播放列表管理视图
2. 选择 "Create New Playlist"
3. 输入播放列表名称
4. 返回主界面，在列表中选中歌曲，按 `A` 添加到播放列表

### 5.10 快捷键速查表

| 分组     | 按键                  | 功能       |
| ------ | ------------------- | -------- |
| **全局** | `q`                 | 退出程序     |
| <br /> | `F1`                | 返回主界面    |
| <br /> | `F2`-`F6`           | 切换功能视图   |
| <br /> | `F7`                | 退出程序     |
| <br /> | `Esc`               | 返回主界面    |
| **焦点** | `C`                 | 焦点到控制区   |
| <br /> | `L`                 | 焦点到列表区   |
| **列表** | `↑`/`↓`             | 选择上/下一首  |
| <br /> | `Space`/`Enter`     | 播放选中     |
| <br /> | `O`                 | 打开文件夹    |
| <br /> | `F`                 | 添加到收藏夹   |
| <br /> | `A`                 | 添加到播放列表  |
| **控制** | `←`/`→`             | 选择控件     |
| <br /> | `Space`             | 激活控件     |
| <br /> | `,`                 | 后退 5 秒   |
| <br /> | `.`                 | 前进 5 秒   |
| <br /> | `D`                 | 跳转到当前歌词行 |
| **歌词** | `Y`                 | 焦点到歌词区   |
| <br /> | `↑`/`↓`             | 选择上/下一句  |
| <br /> | `D`/`Enter`/`Space` | 跳转到选中歌词行 |

### 5.11 终端大小调整

Ter-Music 支持终端窗口大小调整。当您调整终端大小时，程序会自动重新调整布局并重新绘制界面。

### 5.12 退出程序

有两种方式退出：

- 在主界面按 `q`
- 按 `Ctrl+C`（程序会正确清理并退出）

## 6. 技术架构

Ter-Music 采用模块化设计，主要模块包括：

- **main.c**：程序入口，命令行参数处理
- **ui.c**：用户界面渲染，事件处理，布局管理
- **audio.c**：音频解码，FFmpeg 初始化，播放控制
- **playlist.c**：播放列表加载，目录扫描，元数据读取
- **progress.c**：播放进度跟踪
- **lyrics.c**：歌词加载，解析，同步显示
- **menu\_views.c**：多视图管理，设置、历史、收藏夹、播放列表管理
- **defs.h**：全局定义，数据结构声明

## 7. 许可证

本项目采用 [GNU General Public License v3.0](LICENSE) 开源许可证。您可以自由使用、修改和分发本软件，但修改后的衍生作品也必须以相同许可证开源。

## 8. 作者

- **作者**：燕戏竹林
- **邮箱**：<yxzl666xx@outlook.com>
- **项目地址**：<https://gitee.com/yanxi-bamboo-forest/ter-music.git>

## 9. 贡献

欢迎提交 Issue 和 Pull Request！

## 10. 故障排除

**问题：声音无法播放**

- 检查 PulseAudio 服务是否运行：`systemctl status pulseaudio`
- 检查扬声器音量是否开启
- 确认 PulseAudio 已正确配置

**问题：中文显示乱码或CJK字符显示为方块**

- 确保您的终端使用 UTF-8 编码
- 检查系统 locale 设置：`locale` 应该显示 `LC_CTYPE=UTF-8` 或类似
- 如果在 tty 终端中 CJK 字符仍然显示为方块，可以尝试使用 kmscon 替换其中一个 tty，kmscon 对中文等东亚字符有更好的支持

**问题：编译时找不到头文件**

- 请确保安装了所有开发包，参考第 3 章依赖安装命令
- 某些发行版将开发包和运行时分开发，需要安装 \*-devel 或 \*-dev 包

**问题：无法打开某些音频文件**

- 确认您的 FFmpeg 版本支持该格式
- 较新版本的 FFmpeg 支持更多编码格式，建议更新

