# flashalgo_add_algorithm(<target> LOAD_ADDRESS <addr> SOURCES <src>...)
#
# Builds a flash algorithm ELF (<target>.elf) for the current (cross) toolchain:
# no startup files, section GC, the flashalgo linker script and the load address.
# Add CPU flags (e.g. -mcpu=cortex-m4 -mthumb) with target_compile_options/target_link_options.

set(FLASHALGO_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/../ld/flashalgo.ld CACHE FILEPATH "flashalgo linker script")
set(FLASHALGO_YAML_TOOL ${CMAKE_CURRENT_LIST_DIR}/../tools/flashalgo_yaml.py CACHE FILEPATH "flashalgo YAML generator")

function(flashalgo_add_algorithm target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "LOAD_ADDRESS" "SOURCES")
    if(NOT ARG_LOAD_ADDRESS)
        message(FATAL_ERROR "flashalgo_add_algorithm(${target}): LOAD_ADDRESS is required")
    endif()

    add_executable(${target} ${ARG_SOURCES})
    set_target_properties(${target} PROPERTIES SUFFIX ".elf" LINK_DEPENDS ${FLASHALGO_LINKER_SCRIPT})
    target_link_libraries(${target} PRIVATE flashalgo::flashalgo)
    target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(
        ${target} PRIVATE
        -nostartfiles
        --specs=nano.specs # newlib-nano, only for memcpy/memset the compiler may emit
        -Wl,--gc-sections
        -Wl,--no-warn-rwx-segments # code and data share PrgCode on purpose
        -Wl,--defsym=FLASHALGO_LOAD_ADDRESS=${ARG_LOAD_ADDRESS}
        -Wl,-T,${FLASHALGO_LINKER_SCRIPT}
        -Wl,-Map,$<TARGET_FILE_DIR:${target}>/${target}.map
    )
endfunction()
