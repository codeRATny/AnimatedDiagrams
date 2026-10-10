cmake_minimum_required(VERSION 3.25)

set(AD_VERSION "2.1.0" CACHE STRING "Project version (x.y.z); CI takes it from the git tag")

project(AnimatedDiagrams
    VERSION ${AD_VERSION}
    DESCRIPTION "Animated architecture diagram editor: request flows, timers, retries; GIF / WebM / MP4 / PowerPoint / HTML export"
    HOMEPAGE_URL "https://github.com/coderatny/animateddiagrams"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(EMSCRIPTEN)
    # HTML player build (emcmake cmake ...): only ad_engine and player/ are built
    set(BUILD_APP OFF)
    set(BUILD_TESTS OFF)
    set(WITH_LIBAV OFF)
    set(WITH_MERMAID OFF)
    set(REQUIRE_TRANSLATIONS OFF)
    add_compile_options(-Oz -flto -fwasm-exceptions)
    add_link_options(-Oz -flto -fwasm-exceptions)
endif()

option(BUILD_APP "Build the Qt application (UI/ + apps/)" ON)
option(BUILD_TESTS "Build unit tests" OFF)
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors (CI)" OFF)
option(WITH_LIBAV "WebM / MP4 export through the FFmpeg libraries (libav*)" ON)
option(WITH_MERMAID "Mermaid import through merman (Rust, built with Cargo; export works without it)" ON)
option(REQUIRE_TRANSLATIONS "Fail when Qt LinguistTools are missing (otherwise the interface is English only)" OFF)
set(AD_PLAYER_HTML "" CACHE FILEPATH
    "Prebuilt HTML player (player.html from an Emscripten build of player/) embedded for the HTML export; empty -- no HTML export")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

add_subdirectory(src)
if(EMSCRIPTEN)
    add_subdirectory(player)
endif()

if(BUILD_APP)
    add_subdirectory(UI)
    add_subdirectory(apps)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_APP)
    include(Packaging)
endif()
