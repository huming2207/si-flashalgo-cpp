# Toolchain file for bare-metal ARM: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
# Needs arm-none-eabi-gcc >= 16 for C++26 reflection.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_SIZE arm-none-eabi-size CACHE FILEPATH "size tool")

# The compiler check can't link a normal program without startup files
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
