# Exports the sample in every PowerPoint mode and checks the files with tests/pptx_check.py.
# usage: cmake -DAPP=<exe> -DPYTHON=<python> -DINPUT=<json> -DOUT_DIR=<dir> -DMODES=video,gif,animated,morph [-DXSD=<dir>]
#        -P pptx_smoke.cmake
file(MAKE_DIRECTORY "${OUT_DIR}")
set(files)
string(REPLACE "," ";" modes "${MODES}")
foreach(mode IN LISTS modes)
    set(out "${OUT_DIR}/${mode}.pptx")
    file(REMOVE "${out}")
    execute_process(COMMAND "${APP}" --export "${out}" --pptx-mode ${mode} --fps 10 "${INPUT}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT rc EQUAL 0 OR NOT EXISTS "${out}")
        message(FATAL_ERROR "pptx export (${mode}) failed: ${rc}\n${stdout}\n${stderr}")
    endif()
    list(APPEND files "${out}")
endforeach()
# insertion into an existing presentation
set(inserted "${OUT_DIR}/inserted.pptx")
execute_process(COMMAND "${APP}" --export "${inserted}" --pptx-mode animated --pptx-insert "${OUT_DIR}/morph.pptx" --pptx-after 2 "${INPUT}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "pptx insertion failed: ${rc}\n${stdout}\n${stderr}")
endif()
list(APPEND files "${inserted}")
set(xsd_args)
if(XSD)
    set(xsd_args --xsd "${XSD}")
endif()
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/pptx_check.py" ${xsd_args} ${files}
    RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
message(STATUS "${stdout}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "pptx check failed\n${stdout}\n${stderr}")
endif()
