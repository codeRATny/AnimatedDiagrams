cmake_minimum_required(VERSION 3.25)

set(AD_VERSION "2.1.0" CACHE STRING "Project version (x.y.z); CI takes it from the git tag")

project(AnimatedDiagrams
    VERSION ${AD_VERSION}
    DESCRIPTION "Animated architecture diagram editor: request flows, timers, retries; GIF/WebM/MP4 export"
    HOMEPAGE_URL "https://github.com/coderatny/animateddiagrams"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_APP "Build the Qt application (UI/ + apps/)" ON)
option(BUILD_TESTS "Build unit tests" OFF)
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors (CI)" OFF)
option(WITH_LIBAV "WebM / MP4 export through the FFmpeg libraries (libav*)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

add_subdirectory(src)

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
