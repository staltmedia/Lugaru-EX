# LugaruEX launcher for Linux (SDL2 + Dear ImGui + OpenGL 2). Also builds on Windows,
# which is only used to try it out there; the Windows release ships launcher/ instead.
cmake_minimum_required(VERSION 3.16)
project(lugaruex_launcher C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 CONFIG REQUIRED)
set(OpenGL_GL_PREFERENCE LEGACY)
find_package(OpenGL REQUIRED)

set(IMGUI ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui)
add_executable(lugaruex-launcher
    main.cpp
    common.cpp
    banner.cpp
    pad.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/fsutil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/modpack.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/setup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/update.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/miniz/miniz.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../launcher/preview_core.cpp
    ${IMGUI}/imgui.cpp
    ${IMGUI}/imgui_draw.cpp
    ${IMGUI}/imgui_tables.cpp
    ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_sdl2.cpp
    ${IMGUI}/backends/imgui_impl_opengl2.cpp
)
target_include_directories(lugaruex-launcher PRIVATE ${IMGUI} ${IMGUI}/backends ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb ${CMAKE_CURRENT_SOURCE_DIR}/../shared)
target_compile_definitions(lugaruex-launcher PRIVATE IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

if(TARGET SDL2::SDL2-static)
    set(SDL_TARGET SDL2::SDL2-static)
else()
    set(SDL_TARGET SDL2::SDL2)
endif()
target_link_libraries(lugaruex-launcher PRIVATE ${SDL_TARGET} OpenGL::GL)

if(MSVC)
    set_property(TARGET lugaruex-launcher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(lugaruex-launcher PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    target_link_libraries(lugaruex-launcher PRIVATE comdlg32 winhttp ole32 shell32 advapi32)
    set_target_properties(lugaruex-launcher PROPERTIES WIN32_EXECUTABLE ON LINK_FLAGS "/ENTRY:mainCRTStartup")
else()
    # one binary for every distro: the C++ runtime inside, SDL2 inside (it loads X11 / Wayland
    # and the rest at run time), only libc and libGL from the system
    target_compile_options(lugaruex-launcher PRIVATE -Wall -Wno-unused-function)
    target_link_options(lugaruex-launcher PRIVATE -static-libstdc++ -static-libgcc)
    target_link_libraries(lugaruex-launcher PRIVATE ${CMAKE_DL_LIBS} pthread)
endif()
