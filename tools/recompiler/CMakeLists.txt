cmake_minimum_required(VERSION 3.16)

set(RECOMPILER_SOURCES
    main.c
    decoder.c
    analyzer.c
    codegen.c
    symbols.c
)

add_executable(recompiler ${RECOMPILER_SOURCES})

target_include_directories(recompiler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_definitions(recompiler PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(recompiler PRIVATE /W3)
else()
    target_compile_options(recompiler PRIVATE -Wall -Wextra -Wpedantic)
endif()
