# HTML player: ad_engine compiled to WebAssembly (Emscripten) + a canvas renderer.
#
#   emcmake cmake -S . -B build-player -G Ninja && cmake --build build-player
#
# Output: build-player/player/player.html -- one self-contained file (wasm embedded as base64,
# no network access). The application embeds it (-DAD_PLAYER_HTML=<path>) and fills its
# placeholders with a document (src/Export/HtmlPlayer.hpp).

set(AD_PLAYER_CLOSURE ON CACHE BOOL "Minify the Emscripten runtime with the Closure compiler")

add_executable(ad_player_engine Player.cpp)
target_link_libraries(ad_player_engine PRIVATE ad_engine)
ad_set_warnings(ad_player_engine)
set_target_properties(ad_player_engine PROPERTIES SUFFIX ".js")

set(ad_player_exports
    _ad_player_load _ad_player_error _ad_player_duration _ad_player_background _ad_player_bounds
    _ad_player_fonts_changed _ad_player_build _ad_player_frame _ad_player_strings _ad_player_strings_size
    _malloc _free)
list(JOIN ad_player_exports "," ad_player_exports_csv)

target_link_options(ad_player_engine PRIVATE
    -sMODULARIZE=1
    -sEXPORT_NAME=AdPlayerEngine
    -sSINGLE_FILE=1                 # wasm inside the JS: one file, works from file://
    -sSINGLE_FILE_BINARY_ENCODE=0   # as base64: plain ASCII (the binary encoding has raw control bytes)
    -sENVIRONMENT=web
    -sALLOW_MEMORY_GROWTH=1
    -sFILESYSTEM=0
    -sMALLOC=emmalloc
    -sDYNAMIC_EXECUTION=0           # no eval / new Function (strict CSP hosts)
    -sTEXTDECODER=2
    -sINCOMING_MODULE_JS_API=[]
    -sEXPORTED_FUNCTIONS=${ad_player_exports_csv}
    -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPF64,UTF8ToString,stringToUTF8,lengthBytesUTF8)
if(AD_PLAYER_CLOSURE)
    target_link_options(ad_player_engine PRIVATE --closure=1)
endif()

# player.html = shell.html + player.css + player.js + the engine (assemble.cmake)
set(player_html ${CMAKE_CURRENT_BINARY_DIR}/player.html)
add_custom_command(
    OUTPUT ${player_html}
    COMMAND ${CMAKE_COMMAND}
        -DSHELL=${CMAKE_CURRENT_SOURCE_DIR}/shell.html
        -DCSS=${CMAKE_CURRENT_SOURCE_DIR}/player.css
        -DPLAYER_JS=${CMAKE_CURRENT_SOURCE_DIR}/player.js
        -DENGINE_JS=$<TARGET_FILE:ad_player_engine>
        -DVERSION=${PROJECT_VERSION}
        -DOUTPUT=${player_html}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/assemble.cmake
    DEPENDS ad_player_engine shell.html player.css player.js assemble.cmake
    COMMENT "Assembling player.html"
    VERBATIM)
add_custom_target(ad_player ALL DEPENDS ${player_html})
