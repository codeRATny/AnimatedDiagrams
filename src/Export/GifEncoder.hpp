#ifndef _EXPORT_GIF_ENCODER_HPP_
#define _EXPORT_GIF_ENCODER_HPP_

#include <array>
#include <cstdint>
#include <span>
#include <vector>

/// @file GifEncoder.hpp
/// @brief Dependency-free GIF89a encoder: median-cut quantization (RGB 5:5:5)
///        and LZW compression. Every frame has its own palette (up to 256 colors).

namespace ad::gif
{

using Rgb = std::array<uint8_t, 3>;

struct Quantized
{
    std::vector<Rgb>     palette; // 1..256 colors
    std::vector<uint8_t> indices; // palette index per pixel
};

/// Quantize an RGBA frame (alpha is ignored). With at most `max_colors` distinct
/// colors the palette is exact, otherwise median cut over a 5:5:5 histogram.
Quantized Quantize(std::span<const uint8_t> rgba, int max_colors = 256);

/// GIF flavoured LZW (variable code width, LSB-first, not split into sub-blocks).
std::vector<uint8_t> LzwEncode(std::span<const uint8_t> indices, int min_code_size);

/// Frame delays in 1/100 s without accumulating rounding error (min 2 cs).
std::vector<int> FrameDelaysCs(int frames, double fps);

class GifEncoder
{
public:
    /// Throws std::invalid_argument for sizes outside 1..65535.
    GifEncoder(int width, int height, bool loop);
    /// rgba.size() must be width * height * 4.
    void AddFrame(std::span<const uint8_t> rgba, int delay_cs);
    /// Write the trailer and return the file bytes; repeated calls return the same data.
    const std::vector<uint8_t> &Finish();

    [[nodiscard]] int FrameCount() const { return _frames; }

private:
    void _U8(uint8_t v) { _out.push_back(v); }
    void _U16(int v);

    int                  _width;
    int                  _height;
    int                  _frames   = 0;
    bool                 _finished = false;
    std::vector<uint8_t> _out;
};

} // namespace ad::gif

#endif // _EXPORT_GIF_ENCODER_HPP_
