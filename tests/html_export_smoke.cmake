# Smoke test: headless export of the sample to the HTML player (-DAD_PLAYER_HTML builds).
# Checks that the page carries the document JSON and the WebAssembly engine.
# Parameters: APP, INPUT, OUT_DIR
file(REMOVE_RECURSE "${OUT_DIR}/html")
file(MAKE_DIRECTORY "${OUT_DIR}/html")
set(out "${OUT_DIR}/html/sample.html")

execute_process(
    COMMAND "${APP}" --export "${out}" "${INPUT}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 110)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "export failed (rc=${rc})\nstdout: ${stdout}\nstderr: ${stderr}")
endif()
if(NOT EXISTS "${out}")
    message(FATAL_ERROR "missing output ${out}")
endif()

file(READ "${out}" html)
file(READ "${INPUT}" input_json)
string(JSON doc_name GET "${input_json}" meta name)

foreach(needle
        "<script type=\"application/json\" id=\"ad-document\">{\""  # the injected document
        "\"name\":\"${doc_name}\""                                     # ... with the sample's content
        "<title>${doc_name}</title>"                                   # page title
        "id=\"ad-player-options\">{\"autoplay\":true"                 # player options
        "AGFzbQ"                                                       # base64 of the wasm magic "\0asm"
        "AdPlayerEngine")                                              # the Emscripten module
    string(FIND "${html}" "${needle}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "the exported page lacks ${needle}")
    endif()
endforeach()
string(FIND "${html}" "{{AD_" pos)
if(NOT pos EQUAL -1)
    message(FATAL_ERROR "the exported page still has a template placeholder")
endif()

file(SIZE "${out}" size)
message(STATUS "OK ${out} (${size} bytes)")
