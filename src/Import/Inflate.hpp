#ifndef _IMPORT_INFLATE_HPP_
#define _IMPORT_INFLATE_HPP_

#include <cstdint>
#include <span>
#include <vector>

/// @file Inflate.hpp
/// @brief DEFLATE decompressor (RFC 1951) -- used for compressed draw.io diagrams.

namespace ad
{

/// Decompress a raw DEFLATE stream. Throws ad::ParseError on corrupt input.
/// `max_output` guards against decompression bombs.
std::vector<uint8_t> Inflate(std::span<const uint8_t> data, size_t max_output = 256U * 1024U * 1024U);

/// Decompress a zlib stream (RFC 1950 header + DEFLATE); the checksum is not verified.
std::vector<uint8_t> InflateZlib(std::span<const uint8_t> data, size_t max_output = 256U * 1024U * 1024U);

} // namespace ad

#endif // _IMPORT_INFLATE_HPP_
