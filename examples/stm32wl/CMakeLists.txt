# cmake -S examples/stm32wl -B build/stm32wl -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel
# cmake --build build/stm32wl
#
# Produces stm32wle5_algo.elf and stm32wle5_algo.yaml (the Soul Injector target.yaml pieces).

cmake_minimum_required(VERSION 3.30)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/../../cmake/arm-none-eabi.cmake)

project(stm32wl_flashalgo LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. flashalgo)

# Load address must match `load_address` in the programmer config (RAM start + 0x20 as before)
flashalgo_add_algorithm(stm32wle5_algo LOAD_ADDRESS 0x20000020 SOURCES algorithm.cpp)
target_compile_options(stm32wle5_algo PRIVATE -mcpu=cortex-m4 -mthumb -Wall -Wextra)
target_link_options(stm32wle5_algo PRIVATE -mcpu=cortex-m4 -mthumb)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
    TARGET stm32wle5_algo POST_BUILD
    COMMAND ${CMAKE_SIZE} -A $<TARGET_FILE:stm32wle5_algo>
    COMMAND Python3::Interpreter ${FLASHALGO_YAML_TOOL} --name stm32wle5 $<TARGET_FILE:stm32wle5_algo> > stm32wle5_algo.yaml
    BYPRODUCTS stm32wle5_algo.yaml
    VERBATIM
)
