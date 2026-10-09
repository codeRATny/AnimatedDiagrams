find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(animated_diagrams WIN32 MACOSX_BUNDLE AnimatedDiagrams.cpp)

qt_add_resources(animated_diagrams "icons"
    PREFIX "/icons"
    BASE "${PROJECT_SOURCE_DIR}/resources/icons"
    FILES "${PROJECT_SOURCE_DIR}/resources/icons/animated-diagrams.png")

if(WIN32)
    target_sources(animated_diagrams PRIVATE "${PROJECT_SOURCE_DIR}/packaging/windows/app.rc")
endif()

set_target_properties(animated_diagrams PROPERTIES OUTPUT_NAME animated-diagrams)
if(UNIX AND NOT APPLE)
    # function names in crash report stack traces (backtrace_symbols needs the dynamic symbol table)
    set_target_properties(animated_diagrams PROPERTIES ENABLE_EXPORTS ON)
endif()
target_compile_definitions(animated_diagrams PRIVATE AD_VERSION_STRING="${PROJECT_VERSION}")

# ---- translations (Qt Linguist) -----------------------------------------------
# Source strings are English; i18n/*.ts are compiled into the executable (:/i18n).
# `cmake --build build --target update_translations` refreshes the .ts files from the sources.
find_package(Qt6 6.4 QUIET COMPONENTS LinguistTools)
if(Qt6LinguistTools_FOUND)
    file(GLOB_RECURSE AD_TR_SOURCES CONFIGURE_DEPENDS
        ${PROJECT_SOURCE_DIR}/src/*.cpp ${PROJECT_SOURCE_DIR}/src/*.hpp
        ${PROJECT_SOURCE_DIR}/UI/*.cpp ${PROJECT_SOURCE_DIR}/UI/*.hpp
        ${PROJECT_SOURCE_DIR}/apps/*.cpp)
    qt_add_translations(animated_diagrams
        TS_FILES
            ${PROJECT_SOURCE_DIR}/i18n/animated-diagrams_en.ts
            ${PROJECT_SOURCE_DIR}/i18n/animated-diagrams_ru.ts
        SOURCES ${AD_TR_SOURCES}
        RESOURCE_PREFIX /i18n
        # core strings: Tr("context", "text") and AD_TR_NOOP("context", "text")
        LUPDATE_OPTIONS -tr-function-alias translate+=Tr,QT_TRANSLATE_NOOP+=AD_TR_NOOP -locations relative)
elseif(REQUIRE_TRANSLATIONS)
    message(FATAL_ERROR "Qt6 LinguistTools not found (install qt6-l10n-tools + qt6-tools-dev / qt6-qttools-devel)")
else()
    message(WARNING "Qt6 LinguistTools not found: the interface is English only "
                    "(install qt6-l10n-tools + qt6-tools-dev / qt6-qttools-devel)")
endif()
target_link_libraries(animated_diagrams PRIVATE ad_ui)
ad_set_warnings(animated_diagrams)

# ---- install -----------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS animated_diagrams
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .)

if(UNIX AND NOT APPLE)
    set(AD_SHARE_DIR ${CMAKE_INSTALL_DATADIR}/animated-diagrams)
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/animated-diagrams.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES "${PROJECT_SOURCE_DIR}/resources/icons/animated-diagrams.png"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps)
    install(FILES "${PROJECT_SOURCE_DIR}/resources/icons/animated-diagrams.svg"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
else()
    set(AD_SHARE_DIR .)
endif()

# samples, bundled plugins and the agent skill (Linux: share/animated-diagrams, Windows: next to the exe)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/samples/" DESTINATION ${AD_SHARE_DIR}/samples)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/plugins/" DESTINATION ${AD_SHARE_DIR}/plugins)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/skills/" DESTINATION ${AD_SHARE_DIR}/skills)

if(WIN32 AND WITH_LIBAV AND LibAV_RUNTIME_DIR)
    # FFmpeg DLLs (avcodec, avformat, avfilter, avutil, swscale, ...) next to the exe
    install(DIRECTORY "${LibAV_RUNTIME_DIR}/" DESTINATION ${CMAKE_INSTALL_BINDIR} FILES_MATCHING PATTERN "*.dll")
endif()

if(WIN32)
    # DLLs of the imported shared libraries the exe links (pugixml, Qt -- windeployqt below completes Qt)
    install(FILES $<TARGET_RUNTIME_DLLS:animated_diagrams> DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        # vcpkg (vcpkg.json): zlib is an UNKNOWN imported library, not listed by TARGET_RUNTIME_DLLS --
        # install the release DLLs of the manifest's packages
        install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/"
            DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL FILES_MATCHING PATTERN "*.dll")
    endif()
endif()

if(WIN32)
    # windeployqt: Qt libraries and plugins next to the exe (for ZIP / NSIS)
    qt_generate_deploy_app_script(
        TARGET animated_diagrams
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${deploy_script})
endif()
