# ad_core -- all logic without Qt: model, JSON, geometry, engine, scene, timeline,
# plugins, draw.io import, GIF encoder, MCP server.
add_library(ad_core STATIC
    Engine/Design.cpp
    Engine/Effects.cpp
    Engine/Engine.cpp
    Engine/Layout.cpp
    Engine/Scene.cpp
    Engine/Shapes.cpp
    Engine/Templates.cpp
    Export/ExportPlan.cpp
    Export/GifEncoder.cpp
    Export/VideoEncoder.cpp
    Geometry/Geometry.cpp
    Geometry/Path.cpp
    Import/DrawioImporter.cpp
    Import/Inflate.cpp
    Import/XmlReader.cpp
    Interaction/HitTest.cpp
    Io/JsonCodec.cpp
    Io/JsonIo.cpp
    Mcp/DocumentTools.cpp
    Mcp/McpServer.cpp
    Mcp/StdioTransport.cpp
    Model/Catalog.cpp
    Model/Color.cpp
    Model/Document.cpp
    Model/Easing.cpp
    Model/Library.cpp
    Model/Model.cpp
    Model/Registry.cpp
    Model/Sample.cpp
    Plugins/Plugin.cpp
    Plugins/PluginManager.cpp
    Timeline/TimelineLayout.cpp
    Utils/File.cpp
    Utils/Text.cpp
)
add_library(ad::core ALIAS ad_core)

target_include_directories(ad_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(ad_core PUBLIC cxx_std_23)
target_link_libraries(ad_core PUBLIC nlohmann_json::nlohmann_json)
if(WITH_LIBAV)
    target_link_libraries(ad_core PRIVATE LibAV::LibAV)
    target_compile_definitions(ad_core PRIVATE AD_HAVE_LIBAV)
endif()
if(MSVC)
    target_compile_options(ad_core PUBLIC /utf-8)
endif()
ad_set_warnings(ad_core)
