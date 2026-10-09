// The nanosvg implementation, compiled once when nanosvg is available only as a header
// (see cmake/Dependencies.cmake). Packages with a compiled libnanosvg do not use this file.
#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h> // NOLINT(misc-include-cleaner)
