# Ter-Music端闱乐部志
【律守】GPLv3公典，蓝章识之；【撰言】C语，蓝章识之；【御宇】麟纳克斯，绿章识之。
![License: GPLv3](https://img.shields.io/badge/License-GPLv3-blue.svg)
![Language: C](https://img.shields.io/badge/Language-C-blue.svg)
![Platform: Linux](https://img.shields.io/badge/Platform-Linux-green.svg)

## 卷一 本志叙略
### 一 枢要功用
Ter-Music者，清简端闱之符令乐部也，专为麟纳克斯御统而造。凭斐氏FFmpeg以解绎音声，波氏PulseAudio以传布乐响，纽氏ncursesw以营文墨之界。其用赅备，列于左：
- 赅众音之制式，凡MP3、WAV、FLAC、OGG、M4A、AAC、WMA、APE、OPUS之伦，罔不洞达
- 契LRC歌辞之文，循音程以同辉，逐节次以昭焕，毫厘不爽
- 循环之制有四：曰序进、曰单曲回环、曰全帙周流、曰乱序杂陈
- 乐目营理之能，任君创置多组曲帙，随宜调遣
- 珍存所好之章，便疾取览
- 自动录纪播弄之迹，便于回溯
- 志录近所临之乐籍目录，无烦复寻
- 文界色采可自节度，任君定制
- 全凭键符捷操，迅疾无伦
- 音程条贯实时昭显，流转顺滑，可任意跳转

### 二 造作之本旨
本器之造，恪守**清简、捷疾、元本**之宗：
- 体至清简，不藉重轩峻宇之境，所占资源至微
- 端闱元造，纯以文墨为界，宜乎无图之器、幽隐之设，与夫耽符令之流者
- 分曹列伍，部伍明晰，易于缮治增益
- 遵西土Unix之哲，专一事而工，与他器协契无间
- 略无窥伺，不录用户毫末之迹，深敬私隐

### 三 殊胜之德
| 殊德 | 诠解 |
| --- | --- |
| 🚀 耗损至微 | 内存所占，恒不逾十兆，CPU之所役，几于无迹 |
| 🎨 文界焕丽 | 分栏列局，绚然有章，随端闱之修广而自适 |
| 🌍 华夷毕达 | UTF-8之文，靡不洞照，中夏之字，咸得显明 |
| 🔄 恒存其制 | 节度、所珍、往迹，皆自动录存，器重启而如故 |
| 🎯 众视迁转 | 以F2至F7键符，迅疾迁转节度、往迹、曲帙诸视 |
| ⚡ 应感无滞 | 百帧每秒之焕新，音程条贯流转顺滑 |
| 🔧 CMake营构 | 当世营构之制，跨御统之性尤善 |
| 🔊 波氏音枢 | 安固低迟之音声传布 |

### 四 施用之境
- 无图之御宇、幽隐之服器，无轩窗界面而欲播乐者
- 微末之嵌合机括，资源至隘之麟纳克斯器用
- 操符令之工师，临案之际，不假他窗，而得闻乐
- 清简自守之幽人，无取乎繁冗轩窗之制者
- 问学C语、斐氏音术、纽氏文界之造者，此为津梁

### 五 所向之人
- 麟纳克斯之达者、耽符令之幽人
- 嵌合机括之工师、御统之守吏
- 清简自守之流
- 无轩窗界面而欲播乐者
- 问学C语与音声术法之造作者

## 卷二 译纂之境阈
### 一 所御之统
- 通融之统：麟纳克斯内核3.10以上
- 荐举之版：Fedora 30+、Ubuntu 20.04+、Arch Linux新制
- 不通之域：Windows、macOS（若君能移植，不胜忻幸）

### 二 器用之限
| 部伍 | 至卑之限 | 荐举之制 |
| --- | --- | --- |
| **CPU** | 单核1GHz | 双核2GHz以上 |
| **内存** | 64MB可用 | 128MB可用以上 |
| **存储** | 200MB可用 | 1024MB可用以上 |
| **声卡** | 波氏役使运行 | 波氏役使运行 |

### 三 译语之器
- **GCC**：7.0以上
- **Clang**：6.0以上
- **C言典则**：C99以上

### 四 营构之具
- **CMake**：3.10以上
- **Make**：GNU Make 4.0以上
- **pkg-config**：0.29以上

## 卷三 凭藉之属与纳置之符令
### 一 必需之凭藉
| 凭藉之库 | 版限 | 所司之事 |
| --- | --- | --- |
| `ffmpeg-free-devel` | 4.0+ | 音声解绎（libavcodec, libavformat, libswresample, libavutil） |
| `pulseaudio-libs-devel` | 10.0+ | 波氏音声传布 |
| `ncurses-devel` | 6.0+ | 文墨之界，宽字符之持 |
| `pthread-devel` | 系统自带 | 多曹伍并行之制 |
| `cmake` | 3.10+ | 营构之统（译纂时必需） |
| `gcc` | 7.0+ | C言译器（译纂时必需） |
| `make` | - | 营构之具（译纂时必需） |
| `pkg-config` | - | 凭藉检核（译纂时必需） |

### 二 Fedora / RHEL / CentOS 纳置之令
```bash
sudo dnf install cmake gcc make pkg-config
sudo dnf install ffmpeg-free-devel pulseaudio-libs-devel ncurses-devel
```

### 三 Ubuntu / Debian / Linux Mint 纳置之令
```bash
sudo apt update
sudo apt install cmake gcc make pkg-config
sudo apt install libavcodec-dev libavformat-dev libswresample-dev libavutil-dev
sudo apt install libpulse-dev libncursesw5-dev
```

**注**：若斐氏开发之库不可得，当先启universe仓廪：
```bash
sudo add-apt-repository universe
sudo apt update
```

### 四 Arch Linux 纳置之令
```bash
sudo pacman -S cmake gcc make pkg-config
sudo pacman -S ffmpeg pulseaudio ncurses
```

## 卷四 译纂之程叙
### 一 索其源本
```bash
git clone https://gitee.com/yanxi-bamboo-forest/ter-music.git
cd ter-music
```

### 二 立营构之舍
```bash
mkdir build
cd build
```

### 三 节度CMake
```bash
cmake ..
```

CMake将自动检核系统中所有凭藉之库，若有阙失，必明告其误。

**可择之CMake节度**：
```bash
# 自定纳置之前缀（默：/usr/local）
cmake .. -DCMAKE_INSTALL_PREFIX=/usr

# 启调试译纂
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 启译纂优化
cmake .. -DCMAKE_BUILD_TYPE=Release
```

### 四 译纂
```bash
make -j$(nproc)
```

`-j$(nproc)` 尽发CPU所有核芯并行译纂，以速其功。

### 五 纳置（可择）
```bash
sudo make install
```

纳置既毕，君可于端闱直书`ter-music`以启其器。

### 六 除纳（若已纳置）
```bash
cd build
sudo make uninstall
```

### 七 清营构之文
```bash
cd build
make clean
# 或尽除营构之舍
rm -rf build
```

### 八 译纂常患
**患一：波氏之库不可得**
```
解：纳pulseaudio-libs-devel（Fedora）或libpulse-dev（Ubuntu）
```

**患二：ncursesw之库不可得**
```
解：纳ncurses-devel（Fedora）或libncursesw5-dev（Ubuntu）
```

**患三：斐氏之首文不可得**
```
解：纳ffmpeg-devel（Fedora）或libavcodec-dev libavformat-dev...（Ubuntu）
```

### 九 营构脚本之用
本器备二营构之脚本，以造异式之可执行文。

#### 1. AppImage营构脚本
`build-appimage.sh` 脚本，直从源本营构AppImage之式（亦可从已有RPM包转换），通于诸种麟纳克斯发行版。

**用法**：
```bash
# 指定版号直从源本营构（荐举）
./build-appimage.sh -v 1.4.1

# 自动检核版号，直从源本营构
./build-appimage.sh

# 从指定RPM包转换
./build-appimage.sh -r build/rpm/ter-music-1.0.0-1.fc43.x86_64.rpm

# 营构毕留存临时之文（调试用）
./build-appimage.sh --keep-temp

# 显助益之文
./build-appimage.sh --help
```

**所出**：AppImage包将出于`build/appimage/`

#### 2. 可携包营构脚本
`build-portable.sh` 脚本，直从源本营构可携压缩之式（亦可从已有RPM包转换），尽纳必需之凭藉库。

**用法**：
```bash
# 指定版号直从源本营构（荐举）
./build-portable.sh -v 1.4.1

# 自动检核版号，直从源本营构
./build-portable.sh

# 从指定RPM包转换
./build-portable.sh -r build/rpm/ter-music-1.0.0-1.fc43.x86_64.rpm

# 营构毕留存临时之文（调试用）
./build-portable.sh --keep-temp

# 显助益之文
./build-portable.sh --help
```

**所出**：可携包将出于`build/portable/`

**注**：二脚本今已支持直从源本营构。亦可先运行`build-rpm.sh`以生RPM包，然后以`-r`选项指定已有RPM包之路径转换。

#### 3. 如意玲珑Linyaps营构脚本
`build-linyaps.sh` 脚本，直从源本营构如意玲珑之包，宜于deepin/UOS等用玲珑包管之统。

**用法**：
```bash
# 以自动检核之版营构Linyaps包
./build-linyaps.sh

# 指定版号营构
./build-linyaps.sh -v 1.2.3

# 营构毕留存临时之文（调试用）
./build-linyaps.sh --keep-temp

# 显助益之文
./build-linyaps.sh --help
```

**所出**：UAB包与layer文将出于`build/linyaps/`

**纳置运行**：
```bash
# 以ll-cli纳置
ll-cli install build/linyaps/org.yxzl.ter-music_1.0.0_x86_64.uab

# 运行
ll-cli run org.yxzl.ter-music
```

**营构凭藉（Debian/Ubuntu/deepin）**：
```bash
sudo apt install linglong-builder cmake make pkg-config
sudo apt install libncurses-dev libavformat-dev libavcodec-dev libswresample-dev
sudo apt install libavutil-dev libtag1-dev libpulse-dev
```

## 卷五 施用之法
### 一 启其器
**若已纳置**：
```bash
ter-music
```

**若未纳置，直从营构之舍运行**：
```bash
cd build
./ter-music
```

### 二 符令行之参数
```bash
ter-music [OPTIONS]

选项：
  -o, --open <path>    启器时直开指定乐籍目录
  -h, --help           显助益之文
```

**示例**：
```bash
# 启器时开吾之乐籍文件夹
ter-music -o ~/Music

# 显助益之文
ter-music --help
```

### 三 文界局度
启之，则局分三栏，其制如左：
```
┌────────────────────────────┬───────────────┐
│  Play List                 │  [Spectrum]   │
│                            ├───────────────┤
│  歌曲列表区域                │  Lyrics       │
│                            │               │
│                            │ 歌词显示区域    │
│                            │  (或黑胶唱片)   │
│                            │               │
├────────────────────────────┤               │
│   Controls                 │               │
│   [==========>-----]       │               │
│  [<<] [Play/Pause] [>>]    │               │
│  [Stop] [Loop:Off] [Volume]│               │
└────────────────────────────┴───────────────┘
Menu: 选项菜单
```

- **左上方**：乐目曲帙，显当前目录下所有音声之文
- **左下方**：控御之栏，具播弄控御之键与音程条贯
- **右侧**：歌辞显明之域，同步显当前所播之曲文
- **底部**：选项目录，具节度、播弄往迹、所珍、关于、退去诸选项

### 四 常操之法
#### 焦点迁转
| 键符 | 所司之事 |
| --- | --- |
| `C` | 迁焦点于控御之区 |
| `L` | 迁焦点于曲帙之区 |
| `D` | 暂迁焦点于歌辞之区 |

**注**：迁焦点于歌辞之区后，歌辞区将显当前曲之文，然不自动随音程滚转，必再按D键方复其动。且唯焦点在曲帙区时，可迁于歌辞区。

#### 曲帙区之操（焦点在乐目曲帙）
| 键符 | 所司之事 |
| --- | --- |
| `↑` / `↓` | 上下择曲 |
| `Space` / `Enter` | 播所选之曲 |
| `O` / `o` | 开新乐籍文件夹 |
| `F` / `f` | 以所选之曲入珍存之帙 |
| `A` / `a` | 以所选之曲入首卷自定曲帙 |
| `S` | 啟音聲搜求之術，憑拼音字首檢索曲名歌者 |

#### 控御区之操（焦点在控御之栏）
| 键符 | 所司之事 |
| --- | --- |
| `←` / `→` | 左右择控御之键 |
| `Space` | 启当前所选之键 |
| `,` | 退五秒 |
| `.` | 进五秒 |
| `D` / `d` | 跳转至当前歌辞行对应之音程 |
| `-` / `_` | 减音量 |
| `=` / `+` | 增音量 |

**控御键诠解**：
| 键名 | 所司之事 |
| --- | --- |
| `<<` | 上一曲 |
| `Play/Pause` | 播/停 |
| `>>` | 下一曲 |
| `Stop` | 止播 |
| `Loop` | 迁循环之制（Off → Single → List → Random） |
| `Progress` | 音程条贯（显当前播弄之进度） |
| `Volume` | 音量节度（显当前音量百分占比，可调） |

#### 歌辞区之操（焦点在歌辞之域）
| 键符 | 所司之事 |
| --- | --- |
| `↑` / `↓` | 上下择歌辞之行 |
| `D` | 退歌辞区焦点，复其滚转 |

#### 功能键（全域可用）
**功能键（F1-F8）**
| 键符 | 所司之事 |
| --- | --- |
| `F1` | 归主界 |
| `F2` | 开节度之视 |
| `F3` | 开播弄往迹之视 |
| `F4` | 开曲帙营理之视 |
| `F5` | 开珍存之视 |
| `F6` | 开关于之视 |
| `F7` | 迁中英文之界 |
| `F8` | 退其器 |

**备用水数键（Esc后三息内输入）**
| 键符 | 所司之事 |
| --- | --- |
| `Esc` + `1` | 归主界 |
| `Esc` + `2` | 开节度之视 |
| `Esc` + `3` | 开播弄往迹之视 |
| `Esc` + `4` | 开曲帙营理之视 |
| `Esc` + `5` | 开珍存之视 |
| `Esc` + `6` | 开关于之视 |
| `Esc` + `7` | 迁中英文之界 |
| `Esc` + `8` | 退其器 |
| `q` | 退其器 |

### 五 循环之制诠解
| 制名 | 诠解 |
| --- | --- |
| `Off` | 序进，播至曲帙之末则止 |
| `Single` | 单曲回环，重播当前之曲 |
| `List` | 全帙周流，播毕一轮则从头复始 |
| `Random` | 乱序杂陈，随机择下一曲 |

按`Space`启Loop键，可循环迁转其制。

### 六 歌辞显明
本器通自动加载LRC式歌辞之文：
- 歌辞之文必与音声之文同置一目录
- 歌辞之名必与音声之名同，后缀为`.lrc`
- 例：`song.mp3` → `song.lrc`
- 器将随播弄之时，自动焕明当前歌辞之行
- 若不得歌辞之文，歌辞区将显"No lyrics loaded"

### 七 节度之文
节度之文存于`~/.config/ter-music/config`，器初启时将自动创之。

**节度之项**：
- `default_startup_path`：默认启行之目录
- `auto_play_on_start`：启器时自动播弄（0/1）
- `remember_last_path`：记上次所临之目录（0/1）
- 色采主题节度：所有文界元素之前景、背景色

器将自动存其节度，改之即生效。

### 八 数据存贮之所
所有用户数据，皆存于`~/.config/ter-music/`目录之下：
```
~/.config/ter-music/
├── config          # 节度之文
├── history        # 播弄往迹
├── favorites      # 珍存之帙
├── dir_history    # 目录临览往迹
└── playlists/     # 自定曲帙
```

### 九 常施用之程叙
**例：初用之法**
1. 启其器：
   ```bash
   ter-music
   ```
2. 按`O`开文件夹，输入君之乐籍目录路径，例：
   ```
   /home/yourname/Music
   ```
3. 器将扫目录中所有音声之文，显于曲帙之中
4. 以`↑` `↓`择欲闻之曲，按`Space`始播
5. 若有歌辞之文，将自动加载，于右侧同步显明
6. 以`,`与`.`可退/进五秒

**例：以曲入珍存之帙**
1. 于曲帙区择欲存之曲
2. 按`F`，底部状态栏将显"Added to favorites!"
3. 按`F4`可览所有珍存之曲
4. 于珍存之视中，可择而播之

**例：创自定曲帙**
1. 按`F3`入曲帙营理之视
2. 择"Create New Playlist"
3. 输入曲帙之名
4. 归主界，于曲帙中择曲，按`A`入曲帙

### 十 捷键速览
| 分曹 | 键符 | 所司之事 |
| --- | --- | --- |
| **全域** | `q` | 退其器 |
|  | `F1` | 归主界 |
|  | `F2`-`F6` | 迁功能之视 |
|  | `F7` | 迁中英文之界 |
|  | `Esc` | 归主界 |
| **焦点** | `C` | 焦点至控御区 |
|  | `L` | 焦点至曲帙区 |
| **曲帙** | `↑`/`↓` | 择上/下一曲 |
|  | `Space`/`Enter` | 播所选之曲 |
|  | `O` | 开文件夹 |
|  | `F` | 入珍存之帙 |
|  | `A` | 入自定曲帙 |
|  | `S` | 啟拼音搜求之術 |
| **控御** | `←`/`→` | 择控件 |
|  | `Space` | 启控件 |
|  | `,` | 退五秒 |
|  | `.` | 进五秒 |
|  | `D` | 跳转至当前歌辞行 |
| **歌辞** | `D` | 暂迁焦点于歌辞区 |
|  | `↑`/`↓` | 择上/下一句 |
|  | `D`/`Enter`/`Space` | 跳转至所选歌辞行 |

### 十一 端闱修广之调
本器通端闱窗牖修广之调，君改其大小时，器将自动重置局度，重绘文界。

### 十二 退其器
退之之法有三：
- 于主界按`q`
- 按`Ctrl+C`（器将正理而退）
- 于选项目录中择"Exit"（即`F7`键）

## 卷六 术法之架构
本器采分曹营治之制，主干部伍列于左：
- **main.c**：众部之总持，符令行参数之铨叙
- **ui.c**：文界之渲染，事机之应接，局度之营理
- **audio.c**：音声之解绎，斐氏之初始化，播弄之控御
- **playlist.c**：乐目之加载，目录之搜检，元数据之读取
- **progress.c**：音程之追蹑，跳转之节度
- **lyrics.c**：歌辞之加载、解析、同步显明之制
- **menu_views.c**：众视之管摄，节度、往迹、珍存、曲帙之理
- **defs.h**：全域之典则，数据形制之申明

## 卷七 律例
本籍遵GNU General Public License v3.0公许之律。君得自由用之、改之、布之，然所改之裔作，必同此律以开源，无得私匿。

## 卷八 撰者
- **撰者**：燕戏竹林
- **邮驿**：<yxzl666xx@outlook.com>
- **本籍所藏**：<https://gitee.com/yanxi-bamboo-forest/ter-music.git>

## 卷九 襄助之请
君若有疑议、有补益，咸得献Issue与Pull Request，无任忻幸。

## 卷十 祛疑解惑
**患：音声不发**
- 察波氏之役是否运行：`systemctl status pulseaudio`符令验之
- 察扬声器音量是否开启
- 确证波氏之制已得宜

**患：中夏文字乱形，或CJK字符显为方垒**
- 必端闱用UTF-8之编码
- 察系统locale之设：`locale`当显`LC_CTYPE=UTF-8`之伦
- 若于tty端闱中，CJK字符仍乱，可易以kmscon，其于东亚之文，所达尤善

**患：译纂之际不得头文件**
- 必尽装诸凭藉之开发包，详卷三
- 诸统多以开发包与运行之包分置，必装*-devel或*-dev之属

**患：不得开某些音声之文**
- 确证君之斐氏版通其制式
- 新版斐氏，所赅尤广，宜更易之