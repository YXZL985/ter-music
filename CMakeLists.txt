cmake_minimum_required(VERSION 3.10)
project(ter-music C)

set(CMAKE_C_STANDARD 99)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
)

# 查找 PortAudio 库
pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)

set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)

add_executable(ter-music src/main.c)

target_include_directories(ter-music PRIVATE 
    ${FFMPEG_INCLUDE_DIRS}
    ${CURSES_INCLUDE_DIR}
    ${PORTAUDIO_INCLUDE_DIRS}
)

target_link_libraries(ter-music PRIVATE 
    PkgConfig::FFMPEG
    PkgConfig::PORTAUDIO
    ${CURSES_LIBRARIES}
    tinfo
)
