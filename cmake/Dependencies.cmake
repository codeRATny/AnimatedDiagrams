# Third-party dependencies: system packages first (apt/dnf), otherwise a pinned
# release is downloaded with a SHA256 check (Windows / systems without packages).
include(FetchContent)

FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 3.10)
FetchContent_MakeAvailable(nlohmann_json)

if(BUILD_TESTS)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
        URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        FIND_PACKAGE_ARGS NAMES GTest)
    FetchContent_MakeAvailable(googletest)
endif()

# zlib: raw DEFLATE of compressed draw.io pages
find_package(ZLIB REQUIRED)

# pugixml: XML parser (draw.io import)
FetchContent_Declare(pugixml
    URL https://github.com/zeux/pugixml/releases/download/v1.14/pugixml-1.14.tar.gz
    URL_HASH SHA256=2f10e276870c64b1db6809050a75e11a897a8d7456c4be5c6b2e35a11168a015
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake # only the sources: pugixml's own CMakeLists adds install rules
    FIND_PACKAGE_ARGS 1.11)
FetchContent_MakeAvailable(pugixml)
if(NOT TARGET pugixml::pugixml)
    add_library(ad_pugixml STATIC "${pugixml_SOURCE_DIR}/src/pugixml.cpp")
    target_include_directories(ad_pugixml SYSTEM PUBLIC "${pugixml_SOURCE_DIR}/src")
    set_target_properties(ad_pugixml PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_library(pugixml::pugixml ALIAS ad_pugixml)
endif()

# nanosvg: SVG path data of custom node shapes. `nanosvg::nanosvg` is what the project links:
#   1. CMake package with the compiled library (Debian/Ubuntu libnanosvg-dev, vcpkg, ...);
#   2. header-only package (nanosvg.h only) -- the implementation is compiled once in ad_nanosvg;
#   3. otherwise the header of a pinned commit is downloaded with a SHA256 check.
add_library(nanosvg::nanosvg INTERFACE IMPORTED)
find_package(NanoSVG CONFIG QUIET)
if(TARGET NanoSVG::nanosvg)
    target_link_libraries(nanosvg::nanosvg INTERFACE NanoSVG::nanosvg)
    message(STATUS "nanosvg: ${NanoSVG_DIR}")
else()
    find_path(NANOSVG_INCLUDE_DIR nanosvg.h PATH_SUFFIXES nanosvg)
    if(NOT NANOSVG_INCLUDE_DIR)
        set(_nanosvg_commit 93ce879dc4c04a3ef1758428ec80083c38610b1f) # = Debian/Ubuntu 0.0~git20231229, vcpkg 2023-12-29
        FetchContent_Declare(nanosvg
            URL https://raw.githubusercontent.com/memononen/nanosvg/${_nanosvg_commit}/src/nanosvg.h
            URL_HASH SHA256=b0057538cd65b6d8a37a2ef7f5aa3905a14f82964b741534ca61bf20054dcc31
            DOWNLOAD_NO_EXTRACT TRUE)
        FetchContent_MakeAvailable(nanosvg)
        set(NANOSVG_INCLUDE_DIR "${nanosvg_SOURCE_DIR}")
    endif()
    message(STATUS "nanosvg (header only): ${NANOSVG_INCLUDE_DIR}")
    add_library(ad_nanosvg STATIC "${CMAKE_CURRENT_LIST_DIR}/NanoSvgImpl.cpp")
    target_include_directories(ad_nanosvg SYSTEM PUBLIC "${NANOSVG_INCLUDE_DIR}")
    set_target_properties(ad_nanosvg PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_definitions(ad_nanosvg PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    target_link_libraries(nanosvg::nanosvg INTERFACE ad_nanosvg)
endif()

# libav (FFmpeg libraries) for GIF / WebM / MP4 export
if(WITH_LIBAV)
    find_package(LibAV 58)
    if(NOT LibAV_FOUND)
        message(FATAL_ERROR
            "libav (FFmpeg libraries) not found. Install libavcodec-dev libavformat-dev libavfilter-dev libswscale-dev "
            "(Debian/Ubuntu) or libav*-free-devel (Fedora), set FFMPEG_ROOT to an FFmpeg SDK (Windows), "
            "or configure with -DWITH_LIBAV=OFF (PNG export only).")
    endif()
    message(STATUS "libav ${LibAV_VERSION}: GIF / WebM / MP4 export enabled")
endif()
