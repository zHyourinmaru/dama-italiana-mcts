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

# ---- Architecture (x64 or x86) ----
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SDL_ARCH "x64")
else()
    set(SDL_ARCH "x86")
endif()

# ---- SDL2 / SDL2_ttf versions ----
set(SDL2_VERSION     "2.30.10")
set(SDL2_TTF_VERSION "2.22.0")

set(SDL2_DIR     "${CMAKE_SOURCE_DIR}/deps/SDL2-${SDL2_VERSION}")
set(SDL2_TTF_DIR "${CMAKE_SOURCE_DIR}/deps/SDL2_ttf-${SDL2_TTF_VERSION}")

# -----------------------------------------------------------------------
# ensure_sdl2_binaries(<NAME> <LOCAL_DIR> <VERSION> <URL> <SENTINEL_FILE>)
#
# Checks if <LOCAL_DIR>/lib/<arch>/<SENTINEL_FILE> exists.
# If not, downloads <URL> (replacing the literal string "VERSION" with
# <VERSION>) as a zip into deps/ and extracts it in-place.
# The official SDL2 VC zips extract to a top-level folder matching the
# deps/ directory name, so the result lands in the right place.
# -----------------------------------------------------------------------
macro(ensure_sdl2_binaries NAME DIR VERSION URL_TEMPLATE SENTINEL)
    string(REPLACE "VERSION" "${VERSION}" _url "${URL_TEMPLATE}")
    set(_sentinel "${DIR}/lib/${SDL_ARCH}/${SENTINEL}")

    if(NOT EXISTS "${_sentinel}")
        set(_zip "${CMAKE_SOURCE_DIR}/deps/_${NAME}-${VERSION}-VC-download.zip")

        message(STATUS "[deps] ${NAME} ${VERSION} prebuilts missing — downloading…")
        message(STATUS "[deps]   URL: ${_url}")

        file(DOWNLOAD "${_url}" "${_zip}"
            SHOW_PROGRESS
            STATUS  _dl_status
            TLS_VERIFY ON
        )
        list(GET _dl_status 0 _dl_code)
        if(NOT _dl_code EQUAL 0)
            list(GET _dl_status 1 _dl_msg)
            message(FATAL_ERROR
                "[deps] Download of ${NAME} failed: ${_dl_msg}\n"
                "  Please download manually:\n"
                "    ${_url}\n"
                "  and extract into: ${CMAKE_SOURCE_DIR}/deps/")
        endif()

        message(STATUS "[deps] Extracting ${NAME}…")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E tar xf "${_zip}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/deps"
            RESULT_VARIABLE _ex_result
        )
        if(NOT _ex_result EQUAL 0)
            message(FATAL_ERROR "[deps] Extraction of ${NAME} failed (exit code ${_ex_result})")
        endif()

        # Remove the temporary zip
        file(REMOVE "${_zip}")

        if(NOT EXISTS "${_sentinel}")
            message(FATAL_ERROR
                "[deps] ${NAME} was extracted but expected file not found:\n"
                "  ${_sentinel}\n"
                "  The zip may have a different internal layout. "
                "Please extract manually into ${DIR}/")
        endif()

        message(STATUS "[deps] ${NAME} ${VERSION} ready.")
    endif()
endmacro()

# Download SDL2 prebuilt VC binaries if lib/ is absent
ensure_sdl2_binaries(
    SDL2
    "${SDL2_DIR}"
    "${SDL2_VERSION}"
    "https://github.com/libsdl-org/SDL/releases/download/release-VERSION/SDL2-devel-VERSION-VC.zip"
    "SDL2main.lib"
)

# Download SDL2_ttf prebuilt VC binaries if lib/ is absent
ensure_sdl2_binaries(
    SDL2_ttf
    "${SDL2_TTF_DIR}"
    "${SDL2_TTF_VERSION}"
    "https://github.com/libsdl-org/SDL_ttf/releases/download/release-VERSION/SDL2_ttf-devel-VERSION-VC.zip"
    "SDL2_ttf.lib"
)

# ---- SDL2 imported targets ----
add_library(SDL2 SHARED IMPORTED)
set_target_properties(SDL2 PROPERTIES
    IMPORTED_LOCATION             "${SDL2_DIR}/lib/${SDL_ARCH}/SDL2.dll"
    IMPORTED_IMPLIB               "${SDL2_DIR}/lib/${SDL_ARCH}/SDL2.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_DIR}/include"
)

# SDL2main (required on Windows, wraps WinMain)
add_library(SDL2main STATIC IMPORTED)
set_target_properties(SDL2main PROPERTIES
    IMPORTED_LOCATION "${SDL2_DIR}/lib/${SDL_ARCH}/SDL2main.lib"
)

add_library(SDL2_ttf SHARED IMPORTED)
set_target_properties(SDL2_ttf PROPERTIES
    IMPORTED_LOCATION             "${SDL2_TTF_DIR}/lib/${SDL_ARCH}/SDL2_ttf.dll"
    IMPORTED_IMPLIB               "${SDL2_TTF_DIR}/lib/${SDL_ARCH}/SDL2_ttf.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_TTF_DIR}/include"
)

# ---- Source files ----
set(SOURCES
    src/main.c
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

# Math library on Unix/Linux
if(UNIX)
    target_link_libraries(dama_italiana PRIVATE m)
endif()

# ---- Copy DLLs next to the executable after build ----
add_custom_command(TARGET dama_italiana POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL2_DIR}/lib/${SDL_ARCH}/SDL2.dll"
        "$<TARGET_FILE_DIR:dama_italiana>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL2_TTF_DIR}/lib/${SDL_ARCH}/SDL2_ttf.dll"
        "$<TARGET_FILE_DIR:dama_italiana>"
)

# Also copy SDL2_ttf runtime dependencies (freetype, zlib, etc.)
file(GLOB SDL2_TTF_EXTRA_DLLS "${SDL2_TTF_DIR}/lib/${SDL_ARCH}/*.dll")
foreach(DLL_FILE ${SDL2_TTF_EXTRA_DLLS})
    add_custom_command(TARGET dama_italiana POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DLL_FILE}"
            "$<TARGET_FILE_DIR:dama_italiana>"
    )
endforeach()
