cmake_minimum_required(VERSION 3.16)
project(pokemon_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Build the recompiler tool first
add_subdirectory(tools/recompiler)

# Find SDL2 for the game runtime
find_package(SDL2 QUIET)

# Game runtime HAL sources
set(HAL_SOURCES
    src/hal/cpu.c
    src/hal/memory.c
    src/hal/ppu.c
    src/hal/apu.c
    src/hal/timer.c
    src/hal/joypad.c
    src/hal/dma.c
    src/hal/serial.c
    src/hal/interrupts.c
)

# Platform sources
set(PLATFORM_SOURCES
    src/platform/window.c
    src/platform/renderer.c
    src/platform/audio.c
    src/platform/input.c
)

# Common game sources
set(GAME_SOURCES
    src/main.c
    src/config.c
    src/save.c
    ${HAL_SOURCES}
    ${PLATFORM_SOURCES}
)

# Function to add a game target
function(add_game_target GAME_NAME GAME_DEFINE ROM_VARIANT)
    # Collect generated sources - check both per-game and shared banks dir
    file(GLOB GENERATED_SOURCES
        "src/generated/${ROM_VARIANT}/bank_*.c"
        "src/generated/banks/bank_*.c"
    )
    if(EXISTS "${CMAKE_SOURCE_DIR}/src/generated/dispatch.c")
        list(APPEND GENERATED_SOURCES "src/generated/dispatch.c")
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/src/generated/stubs.c")
        list(APPEND GENERATED_SOURCES "src/generated/stubs.c")
    endif()

    if(SDL2_FOUND AND GENERATED_SOURCES)
        add_executable(${GAME_NAME}
            ${GAME_SOURCES}
            ${GENERATED_SOURCES}
        )
        target_compile_definitions(${GAME_NAME} PRIVATE ${GAME_DEFINE}=1)
        if(MSVC)
            target_compile_definitions(${GAME_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
            target_compile_options(${GAME_NAME} PRIVATE /W3)
        endif()
        target_include_directories(${GAME_NAME} PRIVATE
            src
            src/hal
            src/platform
            src/generated
            ${SDL2_INCLUDE_DIRS}
        )
        target_link_libraries(${GAME_NAME} PRIVATE ${SDL2_LIBRARIES})
    endif()
endfunction()

# Game targets (only built after recompilation generates the C files)
add_game_target(pokemon_red GAME_RED red)
add_game_target(pokemon_blue GAME_BLUE blue)
add_game_target(pokemon_yellow GAME_YELLOW yellow)

# BGB demo generator tool (for reference frame capture)
add_executable(gen_bgb_demo tools/gen_bgb_demo.c)
if(MSVC)
    target_compile_definitions(gen_bgb_demo PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
