# Smoke test: MCP session over stdio against the headless application.
# Parameters: APP, INPUT (document to open), OUT_DIR
cmake_policy(SET CMP0057 NEW) # IN_LIST
file(REMOVE_RECURSE "${OUT_DIR}/mcp")
file(MAKE_DIRECTORY "${OUT_DIR}/mcp")
set(saved "${OUT_DIR}/mcp/saved.json")
set(session "${OUT_DIR}/mcp/session.jsonl")

file(WRITE "${session}"
"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},\"clientInfo\":{\"name\":\"smoke\",\"version\":\"1\"}}}
{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}
{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}
{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"get_summary\",\"arguments\":{}}}
{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"add_node\",\"arguments\":{\"label\":\"SmokeNode\",\"type\":\"db\"}}}
{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"render_frame\",\"arguments\":{\"timeMs\":1000,\"scale\":0.5}}}
{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\",\"params\":{\"name\":\"save_document\",\"arguments\":{\"path\":\"${saved}\"}}}
")

execute_process(
    COMMAND "${APP}" --mcp "${INPUT}"
    INPUT_FILE "${session}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 110)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "mcp session failed (rc=${rc})\nstdout: ${stdout}\nstderr: ${stderr}")
endif()

# one JSON-RPC response per line; collect the ids that carry a result
set(answered "")
string(REPLACE ";" "," flat "${stdout}") # keep list splitting on newlines only
string(REPLACE "\n" ";" lines "${flat}")
foreach(line IN LISTS lines)
    if(line STREQUAL "")
        continue()
    endif()
    string(JSON rid ERROR_VARIABLE err GET "${line}" id)
    string(JSON res ERROR_VARIABLE res_err TYPE "${line}" result)
    if(NOT err AND NOT res_err)
        list(APPEND answered ${rid})
    endif()
endforeach()
foreach(id 1 2 3 4 5 6)
    if(NOT id IN_LIST answered)
        message(FATAL_ERROR "no result for request ${id}\nstdout: ${stdout}\nstderr: ${stderr}")
    endif()
endforeach()
if(stdout MATCHES "\"isError\":true")
    message(FATAL_ERROR "a tool call failed\nstdout: ${stdout}")
endif()
if(NOT stdout MATCHES "\"type\":\"image\"")
    message(FATAL_ERROR "render_frame returned no image")
endif()
if(NOT EXISTS "${saved}")
    message(FATAL_ERROR "save_document did not write ${saved}")
endif()
file(READ "${saved}" doc)
if(NOT doc MATCHES "SmokeNode")
    message(FATAL_ERROR "saved document misses the added node")
endif()
message(STATUS "OK MCP stdio session")
