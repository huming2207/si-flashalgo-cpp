# Host tests: the algorithm is compiled for the build machine and the test plays the programmer.

foreach(name full minimal)
    add_executable(test_${name} test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE flashalgo::flashalgo)
    # FLASHALGO_EXPORT aliases symbols with toplevel asm, which needs non-PIC symbol references
    target_compile_options(test_${name} PRIVATE -fno-pie -Wall -Wextra -Werror)
    target_link_options(test_${name} PRIVATE -no-pie)
    add_test(NAME host.${name} COMMAND test_${name})
endforeach()

# The YAML tool reads the self test table back out of a real ELF
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME tool.self_tests COMMAND Python3::Interpreter ${FLASHALGO_YAML_TOOL} --tests-only $<TARGET_FILE:test_full>)
    set_tests_properties(
        tool.self_tests PROPERTIES
        PASS_REGULAR_EXPRESSION "name: \"Clock check\"\n    type: simple\n    addr: 0xfffff004"
    )
endif()

# Compile-fail tests: each file must fail to compile with the message on its `// expect:` line
# (a regex). `// flags:` replaces -fno-exceptions, for the test that checks that flag.
file(GLOB compile_fail_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/*.cpp)
foreach(src IN LISTS compile_fail_sources)
    get_filename_component(name ${src} NAME_WE)
    file(STRINGS ${src} expect REGEX "^// expect: ")
    list(TRANSFORM expect REPLACE "^// expect: " "")
    file(STRINGS ${src} extra_flags REGEX "^// flags: ")
    list(TRANSFORM extra_flags REPLACE "^// flags: " "")
    if(NOT extra_flags)
        set(extra_flags -fno-exceptions)
    endif()

    add_test(
        NAME compile_fail.${name}
        COMMAND ${CMAKE_CXX_COMPILER} -std=gnu++26 -freflection ${extra_flags} -fno-rtti -fsyntax-only
                -I${PROJECT_SOURCE_DIR}/include ${src}
    )
    # Passing on the expected message (not just any failure) proves the right check fired
    set_tests_properties(compile_fail.${name} PROPERTIES PASS_REGULAR_EXPRESSION "${expect}")
endforeach()
