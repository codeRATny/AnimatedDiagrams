execute_process(COMMAND "${APP}" --mcp
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 2 OR NOT output STREQUAL "" OR NOT error MATCHES "--mcp-port")
    message(FATAL_ERROR "Expected --mcp to reject stdio and point to HTTP, got ${status}: ${output}${error}")
endif()
