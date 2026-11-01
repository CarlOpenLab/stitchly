cmake_minimum_required(VERSION 3.15)
project(longshot_core CXX)

add_library(longshot STATIC
    src/image.cpp
    src/align.cpp
    src/stitcher.cpp
    src/api.cpp
)
target_include_directories(longshot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(longshot PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(longshot PRIVATE /O2 /W4)
else()
    target_compile_options(longshot PRIVATE -O2 -Wall -Wextra)
endif()

option(LS_BUILD_TESTS "Build core tests" OFF)
if(LS_BUILD_TESTS)
    enable_testing()
    add_executable(ls_test tests/test_stitch.cpp)
    target_link_libraries(ls_test PRIVATE longshot)
    target_compile_features(ls_test PRIVATE cxx_std_17)
    if(NOT MSVC)
        target_compile_options(ls_test PRIVATE -O2 -Wall -Wextra)
    endif()
    add_test(NAME core_tests COMMAND ls_test)
endif()
