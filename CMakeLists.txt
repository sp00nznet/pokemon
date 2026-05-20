cmake_minimum_required(VERSION 3.16)
project(pokemon_recomp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Build the recompiler tool first
add_subdirectory(tools/recompiler)

# gb-recompiled runtime feature gates.
# The runtime CMakeLists defaults LA_HAS_MULTIPLAYER ON whenever SDL2 is
# found, but the multiplayer overlay (ENet world sync, PvP, trade) is
# Link's Awakening-specific game logic with no meaning for Pokemon. Force
# it off so the ENet submodule and mp_* sources are never compiled.
# LA_HAS_IMGUI is left at its default (on) — the Pokemon debug GUI / asset
# viewer depend on it.
set(LA_HAS_MULTIPLAYER OFF CACHE BOOL "Multiplayer overlay is LA-specific" FORCE)

# Build gb-recompiled runtime (provides gbrt static library + SDL2 + ImGui)
add_subdirectory(gbrecomp/runtime)

# Pokemon-specific sources (runtime bridge + debug)
set(GAME_SOURCES
    src/main.c
    src/pokemon_debug.c
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

    if(GENERATED_SOURCES)
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
            src/generated
        )
        # Link against gb-recompiled runtime (provides gbrt.h, SDL2, ImGui)
        target_link_libraries(${GAME_NAME} PRIVATE gbrt)
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

# Frame comparison tool
add_executable(frame_compare tools/frame_compare.c)
if(MSVC)
    target_compile_definitions(frame_compare PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
