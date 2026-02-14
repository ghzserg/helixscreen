cmake_minimum_required(VERSION 3.10)
project(HelixScreen)

# Project root is three levels up from android/app/jni/
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# ============================================================================
# SDL2
# ============================================================================
set(SDL2_DIR "${PROJECT_ROOT}/lib/sdl2")
add_subdirectory(${SDL2_DIR} SDL2)

# ============================================================================
# spdlog (header-only)
# ============================================================================
set(SPDLOG_DIR "${PROJECT_ROOT}/lib/spdlog")
# Just add include path, no build needed

# ============================================================================
# libhv (static library)
# ============================================================================
set(LIBHV_DIR "${PROJECT_ROOT}/lib/libhv")
# libhv has its own CMakeLists.txt but we need to configure it for Android
set(WITH_OPENSSL OFF CACHE BOOL "" FORCE)
set(WITH_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${LIBHV_DIR} libhv)

# ============================================================================
# TinyGL (static library from source globs)
# TinyGL's CMakeLists.txt has an incomplete source list (missing zdither.c),
# so we build from source globs directly, like we do for LVGL.
# ============================================================================
set(TINYGL_DIR "${PROJECT_ROOT}/lib/tinygl")
file(GLOB TINYGL_SOURCES "${TINYGL_DIR}/src/*.c")
add_library(tinygl STATIC ${TINYGL_SOURCES})
target_include_directories(tinygl PUBLIC "${TINYGL_DIR}/include")
target_compile_options(tinygl PRIVATE -Wno-unused-but-set-variable)

# ============================================================================
# LVGL (static library from source globs)
# ============================================================================
file(GLOB_RECURSE LVGL_SOURCES "${PROJECT_ROOT}/lib/lvgl/src/*.c")
# Also include the C++ thorvg sources
file(GLOB_RECURSE LVGL_CXX_SOURCES "${PROJECT_ROOT}/lib/lvgl/src/*.cpp")

add_library(lvgl STATIC ${LVGL_SOURCES} ${LVGL_CXX_SOURCES})
target_include_directories(lvgl PUBLIC
    "${PROJECT_ROOT}"              # For lv_conf.h at project root
    "${PROJECT_ROOT}/include"      # For lvgl_assert_handler.h
    "${PROJECT_ROOT}/lib/lvgl"     # For lvgl.h
)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE HELIX_DISPLAY_SDL)
# LVGL's SDL driver needs SDL headers
target_link_libraries(lvgl PUBLIC SDL2)

# ============================================================================
# lv_markdown (static library)
# ============================================================================
set(LV_MARKDOWN_DIR "${PROJECT_ROOT}/lib/lv_markdown")
file(GLOB LV_MARKDOWN_SOURCES
    "${LV_MARKDOWN_DIR}/src/*.c"
    "${LV_MARKDOWN_DIR}/deps/md4c/md4c.c"
)
add_library(lv_markdown STATIC ${LV_MARKDOWN_SOURCES})
target_include_directories(lv_markdown PUBLIC
    "${LV_MARKDOWN_DIR}/src"
    "${LV_MARKDOWN_DIR}/deps/md4c"
    "${PROJECT_ROOT}"
    "${PROJECT_ROOT}/include"          # For lvgl_assert_handler.h
    "${PROJECT_ROOT}/lib/lvgl"
)
target_compile_definitions(lv_markdown PUBLIC LV_CONF_INCLUDE_SIMPLE)

# ============================================================================
# HelixScreen main library
# ============================================================================
file(GLOB_RECURSE HELIX_SOURCES "${PROJECT_ROOT}/src/*.cpp")

# Exclude files not needed on Android
list(FILTER HELIX_SOURCES EXCLUDE REGEX ".*/test_.*\\.cpp$")
list(FILTER HELIX_SOURCES EXCLUDE REGEX ".*/helix_splash\\.cpp$")
list(FILTER HELIX_SOURCES EXCLUDE REGEX ".*/helix_watchdog\\.cpp$")
list(FILTER HELIX_SOURCES EXCLUDE REGEX ".*/lvgl-demo/.*")

# Also exclude Objective-C++ files (.mm) - they're macOS only
# (CMake won't glob them with *.cpp anyway, but be explicit)

# Glob font sources
file(GLOB FONT_SOURCES "${PROJECT_ROOT}/assets/fonts/*.c")

# Glob generated translation source
file(GLOB TRANS_SOURCES "${PROJECT_ROOT}/src/generated/*.c")

add_library(main SHARED ${HELIX_SOURCES} ${FONT_SOURCES} ${TRANS_SOURCES})

target_include_directories(main PRIVATE
    "${PROJECT_ROOT}/include"
    "${PROJECT_ROOT}"
    "${PROJECT_ROOT}/lib"                   # For lvgl/lvgl.h, spdlog/spdlog.h etc.
    "${PROJECT_ROOT}/lib/lvgl"              # For direct lvgl includes
    "${PROJECT_ROOT}/lib/lvgl/src"          # For drivers/sdl/lv_sdl_window.h etc.
    "${PROJECT_ROOT}/lib/libhv/include"
    "${PROJECT_ROOT}/lib/libhv/cpputil"     # For json.hpp (nlohmann)
    "${PROJECT_ROOT}/lib/libhv"             # For hv/ includes
    "${PROJECT_ROOT}/lib/spdlog/include"
    "${PROJECT_ROOT}/lib/tinygl/include"
    "${PROJECT_ROOT}/lib/tinygl/include-demo" # For stb_image.h
    "${PROJECT_ROOT}/lib/glm"
    "${PROJECT_ROOT}/lib/lv_markdown/src"
    "${PROJECT_ROOT}/lib/lv_markdown/deps/md4c"
    "${PROJECT_ROOT}/lib/mdns"
    "${PROJECT_ROOT}/lib/cpp-terminal/include"
    "${PROJECT_ROOT}/assets/fonts"
    "${PROJECT_ROOT}/src/generated"
)

# Read version from VERSION.txt
file(READ "${PROJECT_ROOT}/VERSION.txt" HELIX_VERSION)
string(STRIP "${HELIX_VERSION}" HELIX_VERSION)
string(REPLACE "." ";" VERSION_PARTS "${HELIX_VERSION}")
list(GET VERSION_PARTS 0 HELIX_VERSION_MAJOR)
list(GET VERSION_PARTS 1 HELIX_VERSION_MINOR)
list(GET VERSION_PARTS 2 HELIX_VERSION_PATCH)

target_compile_definitions(main PRIVATE
    HELIX_DISPLAY_SDL
    HELIX_PLATFORM_ANDROID
    LV_CONF_INCLUDE_SIMPLE
    HELIX_VERSION="${HELIX_VERSION}"
    HELIX_VERSION_MAJOR=${HELIX_VERSION_MAJOR}
    HELIX_VERSION_MINOR=${HELIX_VERSION_MINOR}
    HELIX_VERSION_PATCH=${HELIX_VERSION_PATCH}
    HELIX_GIT_SHA="android-dev"
)

target_compile_options(main PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++17>
    -Wall
    -Wno-unused-function
)

# Link everything
find_library(log-lib log)
target_link_libraries(main
    SDL2
    lvgl
    lv_markdown
    hv_static
    tinygl
    ${log-lib}
    android
)
