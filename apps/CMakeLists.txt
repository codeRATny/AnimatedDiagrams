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
target_compile_definitions(animated_diagrams PRIVATE AD_VERSION_STRING="${PROJECT_VERSION}")
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

if(WIN32)
    # windeployqt: Qt libraries and plugins next to the exe (for ZIP / NSIS)
    qt_generate_deploy_app_script(
        TARGET animated_diagrams
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${deploy_script})
endif()
