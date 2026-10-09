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

# libav (FFmpeg libraries) for WebM / MP4 export
if(WITH_LIBAV)
    find_package(LibAV 58)
    if(NOT LibAV_FOUND)
        message(FATAL_ERROR
            "libav (FFmpeg libraries) not found. Install libavcodec-dev libavformat-dev libswscale-dev "
            "(Debian/Ubuntu) or libav*-free-devel (Fedora), set FFMPEG_ROOT to an FFmpeg SDK (Windows), "
            "or configure with -DWITH_LIBAV=OFF (GIF / PNG export only).")
    endif()
    message(STATUS "libav ${LibAV_VERSION}: WebM / MP4 export enabled")
endif()

# PowerPoint export: OPC zip container (libzip) and OOXML parts (pugixml)
# pkg-config first: the CMake config of Debian / Ubuntu libzip-dev references the zipcmp / zipmerge
# tools of a separate package and fails without them
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBZIP QUIET IMPORTED_TARGET libzip)
endif()
if(LIBZIP_FOUND)
    add_library(libzip::zip ALIAS PkgConfig::LIBZIP)
else()
    find_package(libzip CONFIG) # vcpkg / Windows
endif()
if(NOT TARGET libzip::zip)
    message(FATAL_ERROR "libzip not found. Install libzip-dev (Debian/Ubuntu), libzip-devel (Fedora) or use vcpkg (Windows).")
endif()
FetchContent_Declare(pugixml
    URL https://github.com/zeux/pugixml/releases/download/v1.14/pugixml-1.14.tar.gz
    URL_HASH SHA256=2f10e276870c64b1db6809050a75e11a897a8d7456c4be5c6b2e35a11168a015
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 1.11)
FetchContent_MakeAvailable(pugixml)
if(NOT TARGET pugixml::pugixml AND TARGET pugixml)
    add_library(pugixml::pugixml ALIAS pugixml)
endif()
