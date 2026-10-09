# The Qt-free core is split in two static libraries:
#
#   ad_engine -- what is needed to load a document and build its frames: model, JSON I/O,
#                geometry, engine / scene (display list), timeline layout, hit testing,
#                export framing, text and i18n helpers. Depends on the C++ standard library
#                and nlohmann_json ONLY: it is also compiled with Emscripten for the HTML
#                player (player/). Do not add libav / zip / XML / zlib / file system code here.
#   ad_core   -- ad_engine + everything else: draw.io import, GIF / video / PowerPoint / HTML export,
#                MCP server, plugins, file helpers, crash handler. The UI, the application
#                and the tests link ad_core (ad::core) and see the same interface as before.
#
# Sources are listed explicitly; a new source file goes to ad_engine only when it keeps
# the dependency rule above.

# ---- ad_engine -------------------------------------------------------------------------
add_library(ad_engine STATIC
    Engine/Design.cpp
    Engine/Effects.cpp
    Engine/Engine.cpp
    Engine/FrameBuffer.cpp
    Engine/Layout.cpp
    Engine/Scene.cpp
    Engine/Shapes.cpp
    Engine/Templates.cpp
    Export/ExportPlan.cpp
    Geometry/Geometry.cpp
    Geometry/Path.cpp
    Interaction/HitTest.cpp
    Io/JsonCodec.cpp
    Io/JsonIo.cpp
    Model/Catalog.cpp
    Model/Color.cpp
    Model/Document.cpp
    Model/Easing.cpp
    Model/Library.cpp
    Model/Markers.cpp
    Model/Model.cpp
    Model/Registry.cpp
    Model/Sample.cpp
    Timeline/TimelineLayout.cpp
    Utils/I18n.cpp
    Utils/Text.cpp
)
add_library(ad::engine ALIAS ad_engine)

target_include_directories(ad_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(ad_engine PUBLIC cxx_std_23)
target_link_libraries(ad_engine PUBLIC nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(ad_engine PUBLIC /utf-8)
endif()
ad_set_warnings(ad_engine)

if(EMSCRIPTEN)
    return() # the HTML player needs the engine only
endif()

# ---- ad_core = ad_engine + import / export / MCP / plugins -----------------------------
add_library(ad_core STATIC
    Export/GifEncoder.cpp
    Export/HtmlPlayer.cpp
    Export/Opc.cpp
    Export/Pptx.cpp
    Export/PptxDeck.cpp
    Export/PptxVector.cpp
    Export/VideoEncoder.cpp
    Import/DrawioImporter.cpp
    Import/Inflate.cpp
    Import/XmlReader.cpp
    Mcp/DocumentTools.cpp
    Mcp/McpServer.cpp
    Mcp/StdioTransport.cpp
    Plugins/Plugin.cpp
    Plugins/PluginManager.cpp
    Utils/CrashHandler.cpp
    Utils/File.cpp
)
add_library(ad::core ALIAS ad_core)

# built-in PowerPoint template (python-pptx default.pptx, MIT) as a byte array
set(AD_PPTX_TEMPLATE ${PROJECT_SOURCE_DIR}/resources/pptx/template.pptx)
set(AD_PPTX_TEMPLATE_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/PptxTemplate.cpp)
add_custom_command(OUTPUT ${AD_PPTX_TEMPLATE_CPP}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${AD_PPTX_TEMPLATE} -DOUTPUT=${AD_PPTX_TEMPLATE_CPP}
            -DNAME=TemplateBytes -DNAMESPACE=ad::pptx -P ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
    DEPENDS ${AD_PPTX_TEMPLATE} ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
    COMMENT "Embedding the PowerPoint template")
target_sources(ad_core PRIVATE ${AD_PPTX_TEMPLATE_CPP})

target_link_libraries(ad_core PUBLIC ad_engine)
target_link_libraries(ad_core PRIVATE libzip::zip pugixml::pugixml) # PowerPoint export
if(WITH_LIBAV)
    target_link_libraries(ad_core PRIVATE LibAV::LibAV)
    target_compile_definitions(ad_core PRIVATE AD_HAVE_LIBAV)
endif()
if(WIN32)
    target_link_libraries(ad_core PRIVATE dbghelp) # crash reports: symbols and minidumps
endif()
ad_set_warnings(ad_core)
