cmake_minimum_required(VERSION 3.30)

project(flashalgo VERSION 0.1.0 LANGUAGES CXX)

set(FLASHALGO_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(FLASHALGO_TOP_LEVEL ON)
endif()

option(FLASHALGO_BUILD_TESTS "Build the host tests (native GCC >= 16)" ${FLASHALGO_TOP_LEVEL})

# Header-only library: include path plus the compile options every algorithm needs
add_library(flashalgo INTERFACE)
add_library(flashalgo::flashalgo ALIAS flashalgo)

target_include_directories(flashalgo INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(flashalgo INTERFACE cxx_std_26)
# GCC >= 15 turns libstdc++ run-time assertions on at -O0; a failing one calls fprintf() and abort(),
# which a flash algorithm can't link (or use). Compile-time checks in constexpr code stay on.
target_compile_definitions(flashalgo INTERFACE _GLIBCXX_NO_ASSERTIONS)
target_compile_options(
    flashalgo INTERFACE
    -freflection
    -fno-exceptions
    -fno-rtti
    -fno-threadsafe-statics
    -fno-use-cxa-atexit
)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/flashalgo.cmake)

if(FLASHALGO_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_subdirectory(tests)
endif()
