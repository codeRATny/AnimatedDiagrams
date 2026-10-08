# ad_core — вся логика без Qt: модель, JSON, геометрия, движок, сцена, таймлайн, GIF.
add_library(ad_core STATIC
    src/catalog.cpp
    src/color.cpp
    src/document.cpp
    src/engine.cpp
    src/export_plan.cpp
    src/geometry.cpp
    src/gif.cpp
    src/hittest.cpp
    src/json_io.cpp
    src/model.cpp
    src/sample.cpp
    src/scene.cpp
    src/timeline.cpp
)
add_library(ad::core ALIAS ad_core)

target_include_directories(ad_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ad_core PUBLIC cxx_std_23)
target_link_libraries(ad_core PRIVATE nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(ad_core PUBLIC /utf-8)
endif()
ad_set_warnings(ad_core)
