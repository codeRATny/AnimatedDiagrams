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
