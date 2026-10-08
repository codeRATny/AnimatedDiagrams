cmake_minimum_required(VERSION 3.25)

set(AD_VERSION "2.0.0" CACHE STRING "Версия проекта (x.y.z); в CI берётся из git-тега")

project(AnimatedDiagrams
    VERSION ${AD_VERSION}
    DESCRIPTION "Редактор анимированных диаграмм: flow-сценарии с сообщениями, таймерами и состояниями; экспорт в GIF/видео"
    HOMEPAGE_URL "https://github.com/coderatny/animateddiagrams"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Тип сборки" FORCE)
endif()

option(AD_BUILD_APP "Собирать Qt-приложение" ON)
option(AD_BUILD_TESTS "Собирать unit-тесты" ON)
option(AD_WARNINGS_AS_ERRORS "Предупреждения как ошибки (для CI)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

add_subdirectory(src/core)

if(AD_BUILD_APP)
    add_subdirectory(src/app)
endif()

if(AD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(AD_BUILD_APP)
    include(Packaging)
endif()
