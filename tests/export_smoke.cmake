# Smoke test: headless export of the sample and a check of the output signature.
# Parameters: APP, INPUT, OUT_DIR, FORMAT (gif | png)
file(REMOVE_RECURSE "${OUT_DIR}/${FORMAT}")
file(MAKE_DIRECTORY "${OUT_DIR}/${FORMAT}")
set(out "${OUT_DIR}/${FORMAT}/sample.${FORMAT}")

execute_process(
    COMMAND "${APP}" --export "${out}" --fps 4 --scale 0.5 "${INPUT}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 110)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "export failed (rc=${rc})\nstdout: ${stdout}\nstderr: ${stderr}")
endif()

if(FORMAT STREQUAL "gif")
    set(check "${out}")
    set(magic "474946383961")  # GIF89a
else()
    set(check "${OUT_DIR}/${FORMAT}/sample_0001.png")
    set(magic "89504e470d0a1a0a")
    if(NOT EXISTS "${OUT_DIR}/${FORMAT}/sample_0049.png")  # 12 s x 4 fps + 1
        message(FATAL_ERROR "expected 49 PNG frames")
    endif()
endif()

if(NOT EXISTS "${check}")
    message(FATAL_ERROR "missing output ${check}")
endif()
string(LENGTH "${magic}" hexlen)
math(EXPR len "${hexlen} / 2")
file(READ "${check}" head LIMIT ${len} HEX)
if(NOT head STREQUAL magic)
    message(FATAL_ERROR "bad signature: ${head}")
endif()
file(SIZE "${check}" size)
message(STATUS "OK ${check} (${size} bytes)")
