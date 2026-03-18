cmake_minimum_required(VERSION 3.10)
project(ter-music C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 查找 ncurses 库
find_package(Curses REQUIRED)

# 包含头文件目录
include_directories(${CURSES_INCLUDE_DIR})

# 添加可执行文件
add_executable(ter-music src/main.c)

# 链接 ncurses 库
target_link_libraries(ter-music ${CURSES_LIBRARIES})