cmake_minimum_required(VERSION 3.16)

project(storpupg)

enable_language(C ASM)

set(TOPDIR ${PROJECT_SOURCE_DIR})

include_directories(${TOPDIR}/src/include)

add_subdirectory(src)
