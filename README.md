# Ter-Music - Terminal Music Player

![License: GPLv3](https://img.shields.io/badge/License-GPLv3-blue.svg)
![Language: C](https://img.shields.io/badge/Language-C-blue.svg)
![Platform: Linux](https://img.shields.io/badge/Platform-Linux-green.svg)

[中文 README](README_CN.md)

## 1. Project Introduction

### 1.1 Core Features

Ter-Music is a lightweight, terminal-based command-line music player designed for Linux systems. It utilizes FFmpeg for audio decoding, PulseAudio for audio output, and provides a beautiful text-based user interface through ncursesw.

**Key Features:**

- 🎵 **Supports Multiple Audio Formats**: MP3, WAV, FLAC, OGG, M4A, AAC, WMA, APE, OPUS and other popular formats
- 📝 **LRC Lyrics Synchronization**: Automatically loads and synchronizes lyrics, highlights current line with playback progress
- 🎶 **Multiple Playback Modes**: Sequential, single repeat, list repeat, shuffle
- 📚 **Playlist Management**: Supports user-defined creation of multiple playlists
- ❤️ **Favorites Feature**: Bookmark favorite songs for quick access
- 🕒 **Playback History**: Automatically records playback history for easy review
- 📂 **Directory History**: Records recently visited music directories
- ⚙️ **Configurable Theme Colors**: Supports custom interface color themes
- ⌨️ **Keyboard Shortcuts**: Full keyboard operation, efficient and convenient
- 📊 **Real-time Progress Bar**: Smooth playback progress display and seeking

### 1.2 Design Philosophy

Ter-Music follows the **simple, efficient, native** design philosophy:

- **Lightweight**: No dependency on heavy desktop environments, extremely low resource usage
- **Terminal Native**: Completely text-based UI, suitable for servers, embedded devices, and users who prefer terminal workflows
- **Modular Design**: Clear module separation, easy to maintain and extend
- **Unix Philosophy**: Do one thing well, work well with other tools
- **No Tracking**: Does not collect any user data, respects privacy

### 1.3 Key Features

| Feature                                      | Description                                 |
| ------------------------------------------ | ------------------------------------------- |
| 🚀 **Low Resource Usage**                     | Memory usage usually < 10MB, extremely low CPU usage  |
| 🎨 **Beautiful TUI**                         | Split-column layout, colored interface, supports terminal size adaptation |
| 🌍 **UTF-8 Chinese Support**                  | Perfect UTF-8 encoding support, correctly displays Chinese song metadata |
| 🔄 **Persistent Storage**                     | Configuration, favorites, and play history are automatically saved and restored after program restart |
| 🎯 **Multiple View Switching**: Quickly switch between settings, history, playlist and other views via F2-F7 function keys | <br /> |
| ⚡ **Responsive UI**: 100 FPS refresh rate, smooth progress bar updates | <br /> |
| 🔧 **CMake Build**: Modern build system, good cross-platform compatibility | <br /> |
| 🔊 **PulseAudio Audio Backend**: Stable low-latency audio output | <br /> |

### 1.4 Use Cases

- **Servers/Headless Systems**: Play music on servers without a graphical interface
- **Embedded Devices**: Run on resource-limited embedded Linux devices
- **Developers**: Code and listen to music while working in the terminal, no need to switch windows
- **Minimalists**: Users who prefer simple software and don't need complex graphical interfaces
- **Learning Reference**: Excellent example project for learning C programming, FFmpeg, and ncurses

### 1.5 Target Audience

- Advanced Linux users and command-line enthusiasts
- Embedded developers and system administrators
- Users pursuing minimalism
- Users who need to play music in environments without a graphical interface
- Developers learning C programming and multimedia programming

## 2. Build Environment Requirements

### 2.1 Operating System

- **Supported Systems**: Linux kernel 3.10 or higher
- **Recommended Distributions**: Fedora 30+, Ubuntu 20.04+, Arch Linux latest
- **Not Supported**: Windows, macOS (contributions for porting are welcome)

### 2.2 Hardware Requirements

| Component | Minimum Requirements | Recommended |
| ------- | --------------- | --------------- |
| **CPU** | Single-core 1GHz | Dual-core 2GHz or higher |
| **Memory** | 64MB available | 128MB available or higher |
| **Storage** | 200MB available disk space | 1024MB available disk space |
| **Sound Card** | PulseAudio service running | PulseAudio service running |

### 2.3 Compiler Versions

- **GCC**: GCC 7.0 or higher
- **Clang**: Clang 6.0 or higher
- **C Standard**: C99 or higher

### 2.4 Build Tools

- **CMake**: 3.10 or higher
- **Make**: GNU Make 4.0 or higher
- **pkg-config**: 0.29 or higher

## 3. Dependencies and Installation Commands

### 3.1 Required Dependencies

| Dependency Library | Version | Purpose |
| ----------------- | ----- | ------------------------------------------------------- |
| `ffmpeg-free-devel` | 4.0+ | Audio decoding (libavcodec, libavformat, libswresample, libavutil) |
| `pulseaudio-libs-devel` | 10.0+ | PulseAudio audio output |
| `ncurses-devel` | 6.0+ | Text user interface, wide character support |
| `pthread-devel` | System | Multi-threading support |
| `cmake` | 3.10+ | Build system (required for compilation) |
| `gcc` | 7.0+ | C compiler (required for compilation) |
| `make` | - | Build tool (required for compilation) |
| `pkg-config` | - | Dependency detection (required for compilation) |

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

**Note**: If you can't find the ffmpeg development packages, you may need to enable the universe repository first:

```bash
sudo add-apt-repository universe
sudo apt update
```

### 3.4 Arch Linux

```bash
sudo pacman -S cmake gcc make pkg-config
sudo pacman -S ffmpeg pulseaudio ncurses
```

## 4. Compilation Steps

### 4.1 Get Source Code

```bash
git clone https://github.com/yanxizhulin/ter-music.git
cd ter-music
```

### 4.2 Create Build Directory

```bash
mkdir build
cd build
```

### 4.3 Configure CMake

```bash
cmake ..
```

CMake will automatically detect all dependencies in your system. If any dependencies are missing, it will display a clear error message.

**Optional CMake Parameters:**

```bash
# Custom installation prefix (default: /usr/local)
cmake .. -DCMAKE_INSTALL_PREFIX=/usr

# Enable debug compilation
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Enable compilation optimizations
cmake .. -DCMAKE_BUILD_TYPE=Release
```

### 4.4 Compile

```bash
make -j$(nproc)
```

`-j$(nproc)` will use all available CPU cores for parallel compilation, speeding up the build process.

### 4.5 Install (Optional)

```bash
sudo make install
```

After installation, you can launch the program by simply typing `ter-music` in your terminal.

### 4.6 Uninstall (if installed)

```bash
cd build
sudo make uninstall
```

### 4.7 Clean Build Files

```bash
cd build
make clean
# Or completely remove the build directory
rm -rf build
```

### 4.8 Common Compilation Issues

**Issue 1: Cannot find PulseAudio library**

```
Solution: Install pulseaudio-libs-devel (Fedora) or libpulse-dev (Ubuntu)
```

**Issue 2: Cannot find ncursesw library**

```
Solution: Install ncurses-devel (Fedora) or libncursesw5-dev (Ubuntu)
```

**Issue 3: Cannot find ffmpeg header files**

```
Solution: Install ffmpeg-devel (Fedora) or libavcodec-dev libavformat-dev... (Ubuntu)
```

### 4.9 Using Build Scripts

Ter-Music provides two build scripts for creating executable files in different formats:

#### 4.9.1 AppImage Build Script

The `build-appimage.sh` script converts RPM packages to AppImage format, suitable for various Linux distributions:

**Usage:**

```bash
# Build AppImage using default RPM package
./build-appimage.sh

# Build AppImage using specified RPM package
./build-appimage.sh -r build/rpm/ter-music-1.0.0-1.fc43.x86_64.rpm

# Keep temporary files after build (for debugging)
./build-appimage.sh --keep-temp
```

**Output:**

- AppImage package will be output to: `build/appimage/`

#### 4.9.2 Portable Package Build Script

The `build-portable.sh` script converts RPM packages to a portable compressed package format, containing all necessary dependency libraries:

**Usage:**

```bash
# Build portable package using default RPM package
./build-portable.sh

# Build portable package using specified RPM package
./build-portable.sh -r build/rpm/ter-music-1.0.0-1.fc43.x86_64.rpm

# Keep temporary files after build (for debugging)
./build-portable.sh --keep-temp
```

**Output:**

- Portable package will be output to: `build/portable/`

**Note:** Both scripts require running `build-rpm.sh` first to generate an RPM package, or use the `-r` option to specify the path to an existing RPM package.

#### 4.9.3 Linyaps Build Script

The `build-linyaps.sh` script builds Linyaps (Linglong) format packages directly from source, suitable for systems like deepin/UOS that use Linglong package management:

**Usage:**

```bash
# Build Linyaps package with automatic version detection
./build-linyaps.sh

# Build with specified version number
./build-linyaps.sh -v 1.2.3

# Keep temporary files after build (for debugging)
./build-linyaps.sh --keep-temp

# Show help information
./build-linyaps.sh --help
```

**Output:**

- UAB package and layer files will be output to: `build/linyaps/`

**Install and Run:**

```bash
# Install using ll-cli
ll-cli install build/linyaps/org.yxzl.ter-music_1.0.0_x86_64.uab

# Run
ll-cli run org.yxzl.ter-music
```

**Build Dependencies (Debian/Ubuntu/deepin):**

```bash
sudo apt install linglong-builder cmake make pkg-config
sudo apt install libncurses-dev libavformat-dev libavcodec-dev libswresample-dev
sudo apt install libavutil-dev libtag1-dev libpulse-dev
```

## 5. Usage

### 5.1 Launch the Program

**If installed:**

```bash
ter-music
```

**If not installed, run directly from build directory:**

```bash
cd build
./ter-music
```

### 5.2 Command Line Arguments

```bash
ter-music [OPTIONS]

Options:
  -o, --open <path>    Open specified music directory directly on startup
  -h, --help           Show help information
```

**Examples:**

```bash
# Open my music folder on startup
ter-music -o ~/Music

# Show help
ter-music --help
```

### 5.3 Interface Layout

After launching, you will see a three-column layout:

```
┌────────────────────────────┬───────────────┐
│  Play List                 │  [Spectrum]   │
│                            ├───────────────┤
│  Song List Area            │  Lyrics       │
│                            │               │
│                            │ Lyrics Display│
│                            │  (Vinyl)      │
│                            │               │
├────────────────────────────┤               │
│   Controls                 │               │
│   [==========>-----]       │               │
│  [<<] [Play/Pause] [>>]    │               │
│  [Stop] [Loop:Off] [Volume]│               │
└────────────────────────────┴───────────────┘
Menu: Options Menu
```

- **Top Left**: Playlist, displays all audio files in the current directory
- **Bottom Left**: Control bar, contains playback control buttons and progress bar
- **Right**: Lyrics display area, synchronously displays lyrics for the currently playing song
- **Bottom**: Options menu, includes settings, playback history, favorites, about, exit, etc.

### 5.4 Basic Operations

#### Focus Switching

| Key | Function |
| --- | -------- |
| `C` | Switch focus to control area |
| `L` | Switch focus to list area |
| `D` | Temporarily switch focus to lyrics area |

- **Note**: After switching focus to the lyrics area, the lyrics area will display the lyrics of the currently playing song, but will not automatically scroll to the current playback position. You need to press D again to resume scrolling. Moreover, you can only switch to lyrics area focus when focus is on the list area.

#### List Area Operations (Focus on Playlist)

| Key | Function |
| ----------------- | ------------------ |
| `↑` / `↓` | Select song up/down |
| `Space` / `Enter` | Play selected song |
| `O` / `o` | Open new music folder |
| `F` / `f` | Add selected song to favorites |
| `A` / `a` | Add selected song to first custom playlist |

#### Control Area Operations (Focus on Control Bar)

| Key | Function |
| --------- | --------------- |
| `←` / `→` | Select control button left/right |
| `Space` | Activate currently selected button |
| `,` (comma) | Seek backward 5 seconds |
| `.` (period) | Seek forward 5 seconds |
| `D` / `d` | Jump to playback position corresponding to current lyrics line |
| `-` / `_` | Decrease volume |
| `=` / `+` | Increase volume |

**Control Button Description:**

| Button | Function |
| ------------ | ------------------------------------ |
| `<<` | Previous track |
| `Play/Pause` | Play/Pause |
| `>>` | Next track |
| `Stop` | Stop playback |
| `Loop` | Switch loop mode (Off → Single → List → Random) |
| `Progress` | Progress bar (shows current playback progress) |
| `Volume` | Volume control (shows current volume percentage, adjustable) |

#### Lyrics Area Operations (Focus on Lyrics Area)

| Key | Function |
| ----------------------------- | --------------- |
| `↑` / `↓` | Select lyrics line up/down |
| `D` | Exit lyrics area focus, resume scrolling |

#### Function Keys (Globally Available)

**Function Keys (F1-F8)**

| Key | Function |
| ----------------- | ------------------ |
| `F1` | Return to main interface |
| `F2` | Open settings view |
| `F3` | Open playback history view |
| `F4` | Open playlist management view |
| `F5` | Open favorites view |
| `F6` | Open about view |
| `F7` | Toggle language (Chinese/English) |
| `F8` | Exit program |

**Alternative Number Keys (Enter within 3 seconds after Esc)**

| Key | Function |
| ----------------- | ------------------ |
| `Esc` + `1` | Return to main interface |
| `Esc` + `2` | Open settings view |
| `Esc` + `3` | Open playback history view |
| `Esc` + `4` | Open playlist management view |
| `Esc` + `5` | Open favorites view |
| `Esc` + `6` | Open about view |
| `Esc` + `7` | Toggle language (Chinese/English) |
| `Esc` + `8` | Exit program |
| `q` | Exit program |

### 5.5 Loop Mode Description

| Mode | Description |
| -------- | --------------- |
| `Off` | Sequential playback, stops at end of list |
| `Single` | Single repeat, repeats current song |
| `List` | List repeat, starts from beginning after playing all |
| `Random` | Shuffle playback, randomly selects next song |

Press `Space` to activate the Loop button to cycle through modes.

### 5.6 Lyrics Display

Ter-Music supports automatic loading of LRC format lyrics files:

- Lyrics files should be placed in the same directory as the audio file
- Lyrics filename should match the audio filename, with extension `.lrc`
- Example: `song.mp3` → `song.lrc`
- The program automatically highlights current lyrics based on playback time
- If no lyrics file is found, the lyrics area will display "No lyrics loaded"

### 5.7 Configuration File

The configuration file is stored at `~/.config/ter-music/config`. The program will automatically create it on first run.

**Configuration options include:**

- `default_startup_path`: Default startup directory
- `auto_play_on_start`: Auto-play on startup (0/1)
- `remember_last_path`: Remember last opened directory (0/1)
- Color theme configuration: Foreground and background colors for all UI elements

The program automatically saves configuration, changes take effect immediately after modification.

### 5.8 Data Storage Location

All user data is stored in the `~/.config/ter-music/` directory:

```
~/.config/ter-music/
├── config          # Configuration file
├── history        # Playback history
├── favorites      # Favorites
├── dir_history    # Directory access history
└── playlists/     # Custom playlists
```

### 5.9 Basic Usage Flow

**Example: First time use**

1. Launch the program:
   ```bash
   ter-music
   ```
2. Press `O` to open folder, enter your music directory path, for example:
   ```
   /home/yourname/Music
   ```
3. The program will scan all audio files in the directory and display them in the playlist
4. Use `↑` `↓` to select the song you want to listen to, press `Space` to start playback
5. If a lyrics file exists, lyrics will be automatically loaded and synchronized on the right side
6. Use `,` and `.` to seek backward/forward 5 seconds

**Example: Add song to favorites**

1. Select the desired song in the list area
2. Press `F`, the bottom status bar will display "Added to favorites!"
3. Press `F5` to view all favorited songs
4. You can select and play favorited songs in the favorites view

**Example: Create custom playlist**

1. Press `F4` to enter playlist management view
2. Select "Create New Playlist"
3. Enter playlist name
4. Return to main interface, select songs in the list, press `A` to add to playlist

### 5.10 Shortcut Cheat Sheet

| Group | Keys | Function |
| ------ | ------------------- | -------- |
| **Global** | `q` | Exit program |
| <br /> | `F1` | Return to main |
| <br /> | `F2`-`F6` | Switch function view |
| <br /> | `F7` | Toggle language |
| <br /> | `F8` | Exit program |
| <br /> | `Esc` | Return to main |
| **Focus** | `C` | Focus to control |
| <br /> | `L` | Focus to list |
| **List** | `↑`/`↓` | Select prev/next |
| <br /> | `Space`/`Enter` | Play selected |
| <br /> | `O` | Open folder |
| <br /> | `F` | Add to favorites |
| <br /> | `A` | Add to playlist |
| **Control** | `←`/`→` | Select control |
| <br /> | `Space` | Activate control |
| <br /> | `,` | Back 5 sec |
| <br /> | `.` | Forward 5 sec |
| <br /> | `D` | Jump to lyric line |
| **Lyrics** | `D` | Temp focus to lyrics |
| <br /> | `↑`/`↓` | Select prev/next line |
| <br /> | `D`/`Enter`/`Space` | Jump to selected line |

### 5.11 Terminal Resizing

Ter-Music supports terminal window resizing. When you resize the terminal, the program will automatically readjust the layout and redraw the interface.

### 5.12 Exit the Program

There are three ways to exit:

- Press `q` in the main interface
- Press `Ctrl+C` (the program will clean up correctly and exit)
- Select "Exit" in the options menu (which is the `F7` key)

## 6. Technical Architecture

Ter-Music adopts a modular design, main modules include:

- **main.c**: Program entry, command-line argument processing
- **ui.c**: User interface rendering, event handling, layout management
- **audio.c**: Audio decoding, FFmpeg initialization, playback control
- **playlist.c**: Playlist loading, directory scanning, metadata reading
- **progress.c**: Playback progress tracking, progress operations such as seeking to specified time
- **lyrics.c**: Lyrics loading, parsing, synchronized display
- **menu_views.c**: Multi-view management, settings, history, favorites, playlist management
- **defs.h**: Global definitions, data structure declarations

## 7. License

This project is licensed under the [GNU General Public License v3.0](LICENSE) open source license. You are free to use, modify, and distribute this software, but modified derivative works must also be open sourced under the same license.

## 8. Author

- **Author**: Yan Xi Zhu Lin
- **Email**: <yxzl666xx@outlook.com>
