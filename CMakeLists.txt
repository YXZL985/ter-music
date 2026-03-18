cmake_minimum_required(VERSION 3.10)
project(ter-music C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 查找 ncurses 库 (优先查找宽字符版本 ncursesw)
find_package(Curses REQUIRED)

# 如果找到的是普通 ncurses，尝试强制指定为 ncursesw (针对中文支持)
# 在许多系统中，find_package(Curses) 可能返回普通库，我们需要显式链接宽字符库
include_directories(${CURSES_INCLUDE_DIR})

# 添加可执行文件
add_executable(ter-music src/main.c)

# 链接 ncurses 库
# 修改：优先链接 ncursesw 以支持宽字符（中文）
# 如果 CURSES_LIBRARIES 不包含 w，则手动替换或追加
if(NOT CURSES_LIBRARIES MATCHES "ncursesw")
    # 尝试直接链接 ncursesw，如果系统支持
    target_link_libraries(ter-music ncursesw)
else()
    target_link_libraries(ter-music ${CURSES_LIBRARIES})
endif()