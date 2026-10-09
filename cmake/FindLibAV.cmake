# Finds the FFmpeg libraries (libavcodec, libavformat, libavutil, libswscale).
#
#   LibAV::LibAV       imported target
#   LibAV_VERSION      libavcodec version
#   LibAV_RUNTIME_DIR  directory with the shared libraries (Windows: bin/ with the DLLs)
#
# pkg-config is tried first (Linux); otherwise FFMPEG_ROOT (CMake or environment
# variable) points to an unpacked SDK with include/, lib/ and bin/ (Windows).
set(_libav_components avcodec avformat avutil swscale)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND AND NOT FFMPEG_ROOT AND NOT DEFINED ENV{FFMPEG_ROOT})
    pkg_check_modules(PC_LIBAV QUIET IMPORTED_TARGET libavcodec libavformat libavutil libswscale)
endif()

if(PC_LIBAV_FOUND)
    set(LibAV_VERSION ${PC_LIBAV_libavcodec_VERSION})
    set(LibAV_INCLUDE_DIR ${PC_LIBAV_INCLUDE_DIRS})
    set(LibAV_LIBRARIES PkgConfig::PC_LIBAV)
    set(LibAV_RUNTIME_DIR ${PC_LIBAV_LIBDIR})
    if(NOT LibAV_INCLUDE_DIR)
        set(LibAV_INCLUDE_DIR ${PC_LIBAV_INCLUDEDIR})
    endif()
    if(NOT LibAV_INCLUDE_DIR)
        set(LibAV_INCLUDE_DIR "/usr/include") # system include path
    endif()
else()
    set(_libav_hints ${FFMPEG_ROOT} $ENV{FFMPEG_ROOT})
    find_path(LibAV_INCLUDE_DIR libavcodec/avcodec.h HINTS ${_libav_hints} PATH_SUFFIXES include)
    set(LibAV_LIBRARIES)
    foreach(_c IN LISTS _libav_components)
        find_library(LibAV_${_c}_LIBRARY NAMES ${_c} HINTS ${_libav_hints} PATH_SUFFIXES lib)
        list(APPEND LibAV_LIBRARIES ${LibAV_${_c}_LIBRARY})
        list(APPEND _libav_required LibAV_${_c}_LIBRARY)
    endforeach()
    if(LibAV_INCLUDE_DIR AND EXISTS "${LibAV_INCLUDE_DIR}/libavcodec/version.h")
        file(STRINGS "${LibAV_INCLUDE_DIR}/libavcodec/version_major.h" _major REGEX "^#define LIBAVCODEC_VERSION_MAJOR")
        file(STRINGS "${LibAV_INCLUDE_DIR}/libavcodec/version.h" _minor REGEX "^#define LIBAVCODEC_VERSION_MINOR")
        string(REGEX MATCH "[0-9]+" _major "${_major}")
        string(REGEX MATCH "[0-9]+" _minor "${_minor}")
        set(LibAV_VERSION "${_major}.${_minor}")
    endif()
    if(LibAV_avcodec_LIBRARY)
        get_filename_component(_libdir "${LibAV_avcodec_LIBRARY}" DIRECTORY)
        if(WIN32)
            get_filename_component(LibAV_RUNTIME_DIR "${_libdir}/../bin" ABSOLUTE)
        else()
            set(LibAV_RUNTIME_DIR "${_libdir}")
        endif()
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibAV
    REQUIRED_VARS LibAV_INCLUDE_DIR LibAV_LIBRARIES ${_libav_required}
    VERSION_VAR LibAV_VERSION)

if(LibAV_FOUND AND NOT TARGET LibAV::LibAV)
    add_library(LibAV::LibAV INTERFACE IMPORTED)
    target_link_libraries(LibAV::LibAV INTERFACE ${LibAV_LIBRARIES})
    if(NOT PC_LIBAV_FOUND)
        target_include_directories(LibAV::LibAV INTERFACE ${LibAV_INCLUDE_DIR})
    endif()
endif()
mark_as_advanced(LibAV_INCLUDE_DIR)
