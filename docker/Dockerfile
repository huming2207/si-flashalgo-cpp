# Development image: native GCC 16 (host tests) + arm-none-eabi-gcc 16 (targets), CMake, Ninja, Python.
# Arch Linux is used because it ships arm-none-eabi-gcc 16 with C++26 reflection.
#
#   docker build -t flashalgo-dev docker
#   docker run --rm -v "$PWD":/src flashalgo-dev /src/scripts/check.sh
FROM archlinux:latest
RUN pacman -Syu --noconfirm gcc arm-none-eabi-gcc arm-none-eabi-newlib arm-none-eabi-binutils cmake ninja python \
    && pacman -Scc --noconfirm
