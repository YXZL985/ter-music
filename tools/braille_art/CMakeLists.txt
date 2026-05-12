cmake_minimum_required(VERSION 3.10)
project(braille_art LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PNG REQUIRED libpng)
pkg_check_modules(JPEG REQUIRED libjpeg)

add_executable(braille_art
    src/main.c
    src/braille_art.c
    src/image_loader.c
)

target_include_directories(braille_art PRIVATE
    include
    ${PNG_INCLUDE_DIRS}
    ${JPEG_INCLUDE_DIRS}
)

target_link_libraries(braille_art
    ${PNG_LIBRARIES}
    ${JPEG_LIBRARIES}
    m
)

target_compile_options(braille_art PRIVATE
    -Wall -Wextra
    ${PNG_CFLAGS_OTHER}
    ${JPEG_CFLAGS_OTHER}
)

target_compile_definitions(braille_art PRIVATE
    _GNU_SOURCE
)
