cmake_minimum_required(VERSION 3.10)
project(ter-music)

set(CMAKE_C_STANDARD 99)

# 查找必要的库
find_package(PkgConfig REQUIRED)
if(NOT PKG_CONFIG_FOUND)
    message(FATAL_ERROR "pkg-config is required but not found.")
endif()

# 使用pkg-config查找依赖库
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample)
pkg_check_modules(AVUTIL REQUIRED libavutil)

# 查找ALSA库
find_library(ALSA_LIBRARY asound)
if(NOT ALSA_LIBRARY)
    message(FATAL_ERROR "ALSA library not found")
endif()

# 查找ncursesw库
find_library(NCURSESW_LIBRARY NAMES ncursesw)
if(NOT NCURSESW_LIBRARY)
    message(FATAL_ERROR "ncursesw library not found")
endif()

# 查找pthread库
find_library(PTHREAD_LIBRARY pthread)
if(NOT PTHREAD_LIBRARY)
    message(FATAL_ERROR "pthread library not found")
endif()

# 添加可执行文件
add_executable(${PROJECT_NAME}
    src/main.c
    src/ui.c
    src/playlist.c
    src/audio.c
    src/progress.c
    src/lyrics.c
    src/menu_views.c    # 新增：菜单视图模块
)

# 包含头文件目录
target_include_directories(${PROJECT_NAME} PRIVATE include /usr/include/ffmpeg)

# 链接库
target_link_libraries(${PROJECT_NAME}
    ${AVCODEC_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${ALSA_LIBRARY}
    ${NCURSESW_LIBRARY}
    ${PTHREAD_LIBRARY}
)

# 添加编译选项
target_compile_options(${PROJECT_NAME} PRIVATE
    ${AVCODEC_CFLAGS_OTHER}
    ${AVFORMAT_CFLAGS_OTHER}
    ${SWRESAMPLE_CFLAGS_OTHER}
    ${AVUTIL_CFLAGS_OTHER}
)

# 添加预处理器定义
target_compile_definitions(${PROJECT_NAME} PRIVATE
    _GNU_SOURCE
)