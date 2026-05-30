cmake_minimum_required(VERSION 3.16)
project(rom C CXX)

# C / C++ standards (platform_sdl.cpp uses C++20 designated initializers)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O3)
endif()

# Runtime lives in the gbrecomp/ submodule at the project root.
set(GBRT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../gbrecomp/runtime")

find_package(SDL2 REQUIRED)

add_library(gbrt STATIC
    ${GBRT_DIR}/src/gbrt.c
    ${GBRT_DIR}/src/ppu.c
    ${GBRT_DIR}/src/audio.c
    ${GBRT_DIR}/src/interpreter.c
    ${GBRT_DIR}/src/hwtrace.c
    ${GBRT_DIR}/src/platform_sdl.cpp
    ${GBRT_DIR}/src/menu_gui.cpp
    ${GBRT_DIR}/src/asset_viewer.cpp
)

# ImGui (bundled in gbrecomp/runtime/third_party/imgui as a nested submodule)
target_sources(gbrt PRIVATE
    ${GBRT_DIR}/third_party/imgui/imgui.cpp
    ${GBRT_DIR}/third_party/imgui/imgui_draw.cpp
    ${GBRT_DIR}/third_party/imgui/imgui_tables.cpp
    ${GBRT_DIR}/third_party/imgui/imgui_widgets.cpp
    ${GBRT_DIR}/third_party/imgui/imgui_demo.cpp
    ${GBRT_DIR}/third_party/imgui/backends/imgui_impl_sdl2.cpp
    ${GBRT_DIR}/third_party/imgui/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(gbrt PUBLIC
    ${GBRT_DIR}/include
    ${GBRT_DIR}/third_party/imgui
)
target_link_libraries(gbrt PUBLIC SDL2::SDL2)
# LA_HAS_IMGUI: build the real menu + asset viewer (not the stubs).
# GB_RECOMPILED_DISPATCH: suppress runtime's weak gb_dispatch fallback
# (rom.c provides the strong definitions; MSVC/COFF doesn't honor weak
# overrides — see gb-recompiled commit 170b8736d).
target_compile_definitions(gbrt PUBLIC GB_HAS_SDL2 LA_HAS_IMGUI GB_RECOMPILED_DISPATCH)

add_executable(rom
    rom_main.c
    rom.c
    rom_rom.c
)
target_link_libraries(rom gbrt)
