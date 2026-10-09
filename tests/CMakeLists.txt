include(GoogleTest)

add_executable(ad_core_tests
    CatalogTest.cpp
    CrashHandlerTest.cpp
    ColorTest.cpp
    DesignSystemTest.cpp
    DocumentTest.cpp
    DocumentToolsTest.cpp
    DrawioImporterTest.cpp
    EasingTest.cpp
    EffectsTest.cpp
    EngineTest.cpp
    ExportPlanTest.cpp
    FileTest.cpp
    FrameBufferTest.cpp
    I18nTest.cpp
    GeometryTest.cpp
    GifEncoderTest.cpp
    HitTestTest.cpp
    HtmlPlayerTest.cpp
    InflateTest.cpp
    JsonIoTest.cpp
    LayoutTest.cpp
    LibraryTest.cpp
    MarkersTest.cpp
    McpServerTest.cpp
    PathTest.cpp
    PptxTest.cpp
    PluginTest.cpp
    RegistryTest.cpp
    SceneTest.cpp
    ShapesTest.cpp
    StdioTransportTest.cpp
    TemplatesTest.cpp
    TextTest.cpp
    TimelineLayoutTest.cpp
    VideoEncoderTest.cpp
    XmlReaderTest.cpp
)
target_link_libraries(ad_core_tests PRIVATE ad::core GTest::gtest_main)
target_compile_definitions(ad_core_tests PRIVATE AD_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples" AD_PLUGINS_DIR="${PROJECT_SOURCE_DIR}/plugins")
ad_set_warnings(ad_core_tests)
gtest_discover_tests(ad_core_tests DISCOVERY_TIMEOUT 60)

# Application smoke tests: headless export and MCP stdio session.
if(TARGET animated_diagrams)
    set(smoke_formats gif png)
    if(WITH_LIBAV)
        list(APPEND smoke_formats webm mp4)
    endif()
    foreach(fmt IN LISTS smoke_formats)
        add_test(NAME AppExportSmoke.${fmt}
            COMMAND ${CMAKE_COMMAND}
                -DAPP=$<TARGET_FILE:animated_diagrams>
                -DINPUT=${PROJECT_SOURCE_DIR}/samples/fallback-retry.json
                -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/smoke
                -DFORMAT=${fmt}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/export_smoke.cmake)
        set_tests_properties(AppExportSmoke.${fmt} PROPERTIES TIMEOUT 120)
    endforeach()
    add_test(NAME AppMcpStdioSmoke
        COMMAND ${CMAKE_COMMAND}
            -DAPP=$<TARGET_FILE:animated_diagrams>
            -DINPUT=${PROJECT_SOURCE_DIR}/samples/fallback-retry.json
            -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/smoke
            -P ${CMAKE_CURRENT_SOURCE_DIR}/mcp_smoke.cmake)
    set_tests_properties(AppMcpStdioSmoke PROPERTIES TIMEOUT 120)
    if(AD_PLAYER_HTML)
        # the HTML player is embedded: export the sample to a page
        add_test(NAME AppExportSmoke.html
            COMMAND ${CMAKE_COMMAND}
                -DAPP=$<TARGET_FILE:animated_diagrams>
                -DINPUT=${PROJECT_SOURCE_DIR}/samples/fallback-retry.json
                -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/smoke
                -P ${CMAKE_CURRENT_SOURCE_DIR}/html_export_smoke.cmake)
        set_tests_properties(AppExportSmoke.html PROPERTIES TIMEOUT 120)
    endif()
endif()

# every interface string of the Russian translation is translated
add_test(NAME TranslationsComplete.ru
    COMMAND ${CMAKE_COMMAND} -DTS_FILE=${PROJECT_SOURCE_DIR}/i18n/animated-diagrams_ru.ts
        -P ${CMAKE_CURRENT_SOURCE_DIR}/translations_check.cmake)

# PowerPoint files read back by an independent implementation (python-pptx) and, with
# -DAD_OOXML_XSD_DIR=<ECMA-376 transitional schemas>, validated against the schemas
if(TARGET animated_diagrams)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        execute_process(COMMAND ${Python3_EXECUTABLE} -c "import pptx, lxml" RESULT_VARIABLE ad_no_pptx OUTPUT_QUIET ERROR_QUIET)
        if(ad_no_pptx EQUAL 0)
            set(pptx_modes gif animated morph)
            if(WITH_LIBAV)
                list(PREPEND pptx_modes video)
            endif()
            list(JOIN pptx_modes "," pptx_modes_arg)
            add_test(NAME AppPptxSmoke
                COMMAND ${CMAKE_COMMAND}
                    -DAPP=$<TARGET_FILE:animated_diagrams>
                    -DPYTHON=${Python3_EXECUTABLE}
                    -DINPUT=${PROJECT_SOURCE_DIR}/samples/fallback-retry.json
                    -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/pptx
                    -DXSD=${AD_OOXML_XSD_DIR}
                    -DMODES=${pptx_modes_arg}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/pptx_smoke.cmake)
            set_tests_properties(AppPptxSmoke PROPERTIES TIMEOUT 300 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        else()
            message(STATUS "python-pptx not found: PowerPoint smoke test disabled")
        endif()
    endif()
endif()
