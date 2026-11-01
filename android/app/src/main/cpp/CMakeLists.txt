cmake_minimum_required(VERSION 3.22)
project(longshot LANGUAGES CXX)

# 共享核心库源码路径：android/app/src/main/cpp -> 工程根/core
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core)

add_library(longshot SHARED
    longshot_jni.cpp
    ${CORE_DIR}/src/image.cpp
    ${CORE_DIR}/src/align.cpp
    ${CORE_DIR}/src/stitcher.cpp
    ${CORE_DIR}/src/api.cpp)

target_include_directories(longshot PRIVATE ${CORE_DIR}/include)
target_compile_features(longshot PRIVATE cxx_std_17)
