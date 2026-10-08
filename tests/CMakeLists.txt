include(GoogleTest)

add_executable(ad_core_tests
    test_color.cpp
    test_document.cpp
    test_engine.cpp
    test_export_plan.cpp
    test_geometry.cpp
    test_gif.cpp
    test_hittest.cpp
    test_json.cpp
    test_scene.cpp
    test_timeline.cpp
)
target_link_libraries(ad_core_tests PRIVATE ad::core GTest::gtest_main)
target_compile_definitions(ad_core_tests PRIVATE AD_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples")
ad_set_warnings(ad_core_tests)
gtest_discover_tests(ad_core_tests DISCOVERY_TIMEOUT 60)

# Смоук-тест приложения: headless-экспорт примера в GIF/PNG (платформа offscreen).
if(AD_BUILD_APP)
    foreach(fmt gif png)
        add_test(NAME app_export_smoke_${fmt}
            COMMAND ${CMAKE_COMMAND}
                -DAPP=$<TARGET_FILE:animated_diagrams>
                -DINPUT=${PROJECT_SOURCE_DIR}/samples/fallback-retry.json
                -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/smoke
                -DFORMAT=${fmt}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/export_smoke.cmake)
        set_tests_properties(app_export_smoke_${fmt} PROPERTIES TIMEOUT 120)
    endforeach()
endif()
