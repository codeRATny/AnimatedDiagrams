# ad_ui -- Qt widgets, rendering, export and the HTTP transport of the MCP server.
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()

qt_add_library(ad_ui STATIC
    AppContext.cpp AppContext.hpp
    ApplyTemplateDialog.cpp ApplyTemplateDialog.hpp
    CanvasWidget.cpp CanvasWidget.hpp
    Controller.cpp Controller.hpp
    CrashReports.cpp CrashReports.hpp
    ExportDialog.cpp ExportDialog.hpp
    ExportManager.cpp ExportManager.hpp
    ExportsPanel.cpp ExportsPanel.hpp
    Exporter.cpp Exporter.hpp
    Fields.cpp Fields.hpp
    Inspector.cpp Inspector.hpp
    Language.cpp Language.hpp
    LibraryDialog.cpp LibraryDialog.hpp
    MainWindow.cpp MainWindow.hpp
    McpHosts.cpp McpHosts.hpp
    McpHttpServer.cpp McpHttpServer.hpp
    PaletteWidget.cpp PaletteWidget.hpp
    PluginsDialog.cpp PluginsDialog.hpp
    PresentationExport.cpp PresentationExport.hpp
    PresenterWindow.cpp PresenterWindow.hpp
    PreviewWidget.cpp PreviewWidget.hpp
    QtRender.cpp QtRender.hpp
    Theme.cpp Theme.hpp
    TimelinePanel.cpp TimelinePanel.hpp
    TimelineWidget.cpp TimelineWidget.hpp
    TitleBar.cpp TitleBar.hpp
)
target_include_directories(ad_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ad_ui PUBLIC QT_NO_KEYWORDS)
target_link_libraries(ad_ui PUBLIC ad_core Qt6::Widgets Qt6::Network)
if(WIN32)
    target_link_libraries(ad_ui PRIVATE dwmapi) # title bar colors of system frames
endif()
ad_set_warnings(ad_ui)

# HTML player (player/ built with Emscripten): embedded as :/player/player.html when
# AD_PLAYER_HTML points to a prebuilt player.html; without it the HTML export is not offered.
if(AD_PLAYER_HTML)
    if(NOT EXISTS "${AD_PLAYER_HTML}")
        message(FATAL_ERROR "AD_PLAYER_HTML: ${AD_PLAYER_HTML} does not exist (build player/ with emcmake first)")
    endif()
    set_source_files_properties("${AD_PLAYER_HTML}" PROPERTIES QT_RESOURCE_ALIAS player.html)
    qt_add_resources(ad_ui "player" PREFIX "/player" FILES "${AD_PLAYER_HTML}")
    message(STATUS "HTML player: ${AD_PLAYER_HTML}")
else()
    message(STATUS "HTML player: not embedded (set AD_PLAYER_HTML to enable the HTML export)")
endif()
