# Generates a C++ source with the bytes of a file:
#   cmake -DINPUT=<file> -DOUTPUT=<file.cpp> -DNAME=<function> -DNAMESPACE=<ns> -P EmbedFile.cmake
# The function returns std::span<const uint8_t> over the embedded data.
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" len)
math(EXPR size "${len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}.tmp" "// generated from ${INPUT} -- do not edit\n#include <cstdint>\n#include <span>\n\nnamespace ${NAMESPACE}\n{\n\nnamespace\n{\nconst uint8_t kData[${size}] = {\n${bytes}};\n} // namespace\n\nstd::span<const uint8_t> ${NAME}() { return kData; }\n\n} // namespace ${NAMESPACE}\n")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
