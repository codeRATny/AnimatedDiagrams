# Fails when a translation file has unfinished (untranslated) messages.
# usage: cmake -DTS_FILE=<file.ts> -P translations_check.cmake
file(READ "${TS_FILE}" content)
string(REGEX MATCHALL "type=\"unfinished\"" unfinished "${content}")
list(LENGTH unfinished count)
if(count GREATER 0)
    message(FATAL_ERROR "${TS_FILE}: ${count} untranslated message(s); run the update_translations target and translate them")
endif()
string(REGEX MATCHALL "<message" messages "${content}")
list(LENGTH messages total)
message(STATUS "${TS_FILE}: ${total} messages, all translated")
