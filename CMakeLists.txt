cmake_minimum_required(VERSION 3.16)
project(dama_italiana C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---- Compiler warnings & optimisation ----
if(MSVC)
    # Replace CMake's default /W3 with /W4 to avoid override warning
    string(REGEX REPLACE "/W[0-3]" "/W4" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    # Release gets /O2; Debug keeps MSVC defaults (/Od /RTC1)
    add_compile_options("$<$<CONFIG:Release>:/O2>")
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options("$<$<CONFIG:Release>:-O2>")
endif()

# ---- Include directories ----
include_directories(${CMAKE_SOURCE_DIR}/include)

# ---- SDL2 from local deps (no vcpkg needed) ----
set(SDL2_DIR     "${CMAKE_SOURCE_DIR}/deps/SDL2-2.30.10")
set(SDL2_TTF_DIR "${CMAKE_SOURCE_DIR}/deps/SDL2_ttf-2.22.0")

# Architecture (x64 or x86)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SDL_ARCH "x64")
else()
    set(SDL_ARCH "x86")
endif()

# SDL2 imported library
add_library(SDL2 SHARED IMPORTED)
set_target_properties(SDL2 PROPERTIES
    IMPORTED_LOCATION "${SDL2_DIR}/lib/${SDL_ARCH}/SDL2.dll"
    IMPORTED_IMPLIB   "${SDL2_DIR}/lib/${SDL_ARCH}/SDL2.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_DIR}/include"
)

# SDL2main imported library (required on Windows)
add_library(SDL2main STATIC IMPORTED)
set_target_properties(SDL2main PROPERTIES
    IMPORTED_LOCATION "${SDL2_DIR}/lib/${SDL_ARCH}/SDL2main.lib"
)

# SDL2_ttf imported library
add_library(SDL2_ttf SHARED IMPORTED)
set_target_properties(SDL2_ttf PROPERTIES
    IMPORTED_LOCATION "${SDL2_TTF_DIR}/lib/${SDL_ARCH}/SDL2_ttf.dll"
    IMPORTED_IMPLIB   "${SDL2_TTF_DIR}/lib/${SDL_ARCH}/SDL2_ttf.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_TTF_DIR}/include"
)

# ---- Source files ----
set(SOURCES
    "src/main.c"
    src/bitboard.c
    src/movegen.c
    src/game.c
    src/pool.c
    src/mcts.c
    src/gui.c
    src/renderer_sdl2.c
    src/tuning.c
)

# ---- Executable ----
add_executable(dama_italiana ${SOURCES})

# ---- Link ----
target_link_libraries(dama_italiana PRIVATE
    SDL2main
    SDL2
    SDL2_ttf
)

# Math library on Unix
if(UNIX)
    target_link_libraries(dama_italiana PRIVATE m)
endif()

# ---- Copy DLLs to output directory ----
add_custom_command(TARGET dama_italiana POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL2_DIR}/lib/${SDL_ARCH}/SDL2.dll"
        "$<TARGET_FILE_DIR:dama_italiana>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL2_TTF_DIR}/lib/${SDL_ARCH}/SDL2_ttf.dll"
        "$<TARGET_FILE_DIR:dama_italiana>"
)

# Also copy any SDL2_ttf runtime dependencies (freetype, etc.)
file(GLOB SDL2_TTF_EXTRA_DLLS "${SDL2_TTF_DIR}/lib/${SDL_ARCH}/*.dll")
foreach(DLL_FILE ${SDL2_TTF_EXTRA_DLLS})
    add_custom_command(TARGET dama_italiana POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DLL_FILE}"
            "$<TARGET_FILE_DIR:dama_italiana>"
    )
endforeach()
