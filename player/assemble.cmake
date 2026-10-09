# Builds player.html from shell.html by inlining the stylesheet, the renderer and the
# Emscripten module (which already carries the wasm as base64).
# Parameters: SHELL, CSS, PLAYER_JS, ENGINE_JS, VERSION, OUTPUT
cmake_minimum_required(VERSION 3.25)

file(READ "${SHELL}" html)
file(READ "${CSS}" css)
file(READ "${PLAYER_JS}" player_js)
file(READ "${ENGINE_JS}" engine_js)

# inlined code must not close its <script> / <style> element early
foreach(part player_js engine_js css)
    string(TOLOWER "${${part}}" lower)
    string(FIND "${lower}" "</script" p1)
    string(FIND "${lower}" "</style" p2)
    if(NOT p1 EQUAL -1 OR NOT p2 EQUAL -1)
        message(FATAL_ERROR "${part} contains a closing </script> or </style> tag")
    endif()
endforeach()

# the placeholders the application fills must occur exactly once (in the shell only)
foreach(ph "{{AD_TITLE}}" "{{AD_PLAYER_OPTIONS}}" "{{AD_DOCUMENT_JSON}}")
    string(FIND "${html}" "${ph}" first)
    string(FIND "${html}" "${ph}" last REVERSE)
    if(first EQUAL -1 OR NOT first EQUAL last)
        message(FATAL_ERROR "shell.html must contain ${ph} exactly once")
    endif()
    foreach(part player_js engine_js css)
        string(FIND "${${part}}" "${ph}" p)
        if(NOT p EQUAL -1)
            message(FATAL_ERROR "${part} must not contain ${ph}")
        endif()
    endforeach()
endforeach()

# string(REPLACE) keeps the inserted text verbatim (no regex / variable expansion)
string(REPLACE "/*@AD_PLAYER_CSS@*/" "${css}" html "${html}")
string(REPLACE "/*@AD_ENGINE_JS@*/" "${engine_js}" html "${html}")
string(REPLACE "/*@AD_PLAYER_JS@*/" "${player_js}" html "${html}")
string(REPLACE "@AD_VERSION@" "${VERSION}" html "${html}")

file(WRITE "${OUTPUT}" "${html}")
file(SIZE "${OUTPUT}" size)
math(EXPR kib "${size} / 1024")
message(STATUS "player.html: ${kib} KiB")
