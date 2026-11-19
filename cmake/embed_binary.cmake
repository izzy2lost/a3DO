# embed_binary(<input file> <output .cpp> <C++ symbol>)
#
# Turns a binary file into a C++ translation unit holding it as a byte array,
# in namespace retro3do, with <symbol> and <symbol>_size. Done at configure
# time with CMake alone so no build host needs xxd or a script interpreter.
# The output is only rewritten when its bytes change, so reconfiguring does not
# force a rebuild.
function(embed_binary input output symbol)
    file(READ "${input}" hex HEX)
    string(LENGTH "${hex}" hex_length)
    math(EXPR byte_count "${hex_length} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    set(content
"// Generated from ${input} by cmake/embed_binary.cmake. Do not edit.
#include <cstddef>
namespace retro3do {
extern const unsigned char ${symbol}[];
extern const std::size_t ${symbol}_size;
alignas(4) const unsigned char ${symbol}[] = {
    ${bytes}
};
const std::size_t ${symbol}_size = ${byte_count};
}  // namespace retro3do
")
    file(WRITE "${output}.tmp" "${content}")
    configure_file("${output}.tmp" "${output}" COPYONLY)
    file(REMOVE "${output}.tmp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
endfunction()
