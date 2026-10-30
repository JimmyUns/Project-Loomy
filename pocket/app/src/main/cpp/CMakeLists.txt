# The phone side runs the very same protocol code as Loomy on the PC: src/pocket.c + Monocypher.
cmake_minimum_required(VERSION 3.22.1)
project(pocket C)
set(LOOMY_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src)
add_library(pocket SHARED
    pocket_jni.c
    ${LOOMY_SRC}/pocket.c
    ${LOOMY_SRC}/monocypher.c
    ${LOOMY_SRC}/monocypher-ed25519.c)
target_include_directories(pocket PRIVATE ${LOOMY_SRC})
target_compile_options(pocket PRIVATE -O2 -Wall -Wno-unused-parameter)
target_link_options(pocket PRIVATE -Wl,--gc-sections -s)
