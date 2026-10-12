#!/usr/bin/env bash
# Builds and runs everything: host tests, compile-fail tests, and the STM32WL example for Cortex-M4.
# Needs GCC >= 16 and arm-none-eabi-gcc >= 16 (see docker/Dockerfile). Builds out of tree.
set -euo pipefail

src="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$(mktemp -d)}"

echo "== host tests (${build}/host)"
cmake -S "$src" -B "$build/host" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$build/host"
ctest --test-dir "$build/host" --output-on-failure

echo "== STM32WL example (${build}/stm32wl)"
cmake -S "$src/examples/stm32wl" -B "$build/stm32wl" -G Ninja
cmake --build "$build/stm32wl"

elf="$build/stm32wl/stm32wle5_algo.elf"
if [ -n "$(arm-none-eabi-nm -u "$elf")" ]; then
    echo "unexpected undefined symbols in $elf" >&2
    exit 1
fi
echo "== $(arm-none-eabi-size -A "$elf" | grep PrgCode)"
echo "== self tests:"
sed -n '/^self_tests:/,$p' "$build/stm32wl/stm32wle5_algo.yaml"
