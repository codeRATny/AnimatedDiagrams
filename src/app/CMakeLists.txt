find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(animated_diagrams WIN32 MACOSX_BUNDLE
    main.cpp
    canvas_widget.cpp canvas_widget.hpp
    controller.cpp controller.hpp
    export_dialog.cpp export_dialog.hpp
    exporter.cpp exporter.hpp
    inspector.cpp inspector.hpp
    main_window.cpp main_window.hpp
    qt_render.cpp qt_render.hpp
    theme.cpp theme.hpp
    timeline_widget.cpp timeline_widget.hpp
)

qt_add_resources(animated_diagrams "icons"
    PREFIX "/icons"
    BASE "${PROJECT_SOURCE_DIR}/resources/icons"
    FILES "${PROJECT_SOURCE_DIR}/resources/icons/animated-diagrams.png")

if(WIN32)
    target_sources(animated_diagrams PRIVATE "${PROJECT_SOURCE_DIR}/packaging/windows/app.rc")
endif()

set_target_properties(animated_diagrams PROPERTIES OUTPUT_NAME animated-diagrams)
target_compile_definitions(animated_diagrams PRIVATE
    AD_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(animated_diagrams PRIVATE ad::core Qt6::Widgets)
ad_set_warnings(animated_diagrams)

# ---- установка ---------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS animated_diagrams
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .)

if(UNIX AND NOT APPLE)
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/animated-diagrams.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES "${PROJECT_SOURCE_DIR}/resources/icons/animated-diagrams.png"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps)
    install(FILES "${PROJECT_SOURCE_DIR}/resources/icons/animated-diagrams.svg"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES "${PROJECT_SOURCE_DIR}/samples/fallback-retry.json"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/animated-diagrams/samples)
endif()

if(WIN32)
    # windeployqt: Qt-библиотеки и плагины рядом с exe (для ZIP/NSIS)
    qt_generate_deploy_app_script(
        TARGET animated_diagrams
        OUTPUT_SCRIPT deploy_script
        NO_TRANSLATIONS
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${deploy_script})
    install(FILES "${PROJECT_SOURCE_DIR}/samples/fallback-retry.json" DESTINATION samples)
endif()
