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
    LibraryDialog.cpp LibraryDialog.hpp
    MainWindow.cpp MainWindow.hpp
    McpHosts.cpp McpHosts.hpp
    McpHttpServer.cpp McpHttpServer.hpp
    PaletteWidget.cpp PaletteWidget.hpp
    PluginsDialog.cpp PluginsDialog.hpp
    PreviewWidget.cpp PreviewWidget.hpp
    QtRender.cpp QtRender.hpp
    Theme.cpp Theme.hpp
    TimelinePanel.cpp TimelinePanel.hpp
    TimelineWidget.cpp TimelineWidget.hpp
)
target_include_directories(ad_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ad_ui PUBLIC QT_NO_KEYWORDS)
target_link_libraries(ad_ui PUBLIC ad_core Qt6::Widgets Qt6::Network)
ad_set_warnings(ad_ui)
