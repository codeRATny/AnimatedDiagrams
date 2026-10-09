#ifndef _ENGINE_FRAME_BUFFER_HPP_
#define _ENGINE_FRAME_BUFFER_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "Engine/DisplayList.hpp"

/// @file FrameBuffer.hpp
/// @brief Flat numeric encoding of a display list (Frame) for renderers outside C++:
///        the HTML player reads it from WebAssembly memory as a Float64Array plus a
///        UTF-8 string blob (player/player.js is the decoder -- keep both in sync).
///
/// Layout (every value is a double):
///
///     header : kFrameBufferVersion, item_count
///     item   : shape_tag, opacity, effect, flags, [fill], [stroke], [transform], [clip], shape
///       flags     : bit 0 fill, bit 1 stroke, bit 2 transform, bit 3 clip
///       fill      : rgb (0xRRGGBB)
///       stroke    : rgb, width, round_cap, dash_offset, dash_count, dash...
///       transform : origin.x, origin.y, rotate_deg, scale, translate.x, translate.y
///       clip      : path
///     shapes (by shape_tag):
///       1 rect    : x, y, w, h, radius
///       2 ellipse : cx, cy, rx, ry
///       3 path    : path
///       4 arc     : cx, cy, radius, start_deg, sweep_deg
///       5 text    : x, y, text_offset, text_bytes, size, bold, family_offset, family_bytes,
///                   align (0 left, 1 center, 2 right), valign (0 baseline, 1 middle),
///                   has_halo, [halo rgb, halo width]
///     path   : segment_count, then per segment its kind (Path::Kind as a number) and
///              points: Move / Line -- x, y; Quad -- c1, to; Cubic -- c1, c2, to; Close -- none
///
/// Strings are byte ranges (offset, length) of the UTF-8 blob Strings().

namespace ad
{

inline constexpr int kFrameBufferVersion = 1;

enum class ShapeTag : uint8_t
{
    Rect    = 1,
    Ellipse = 2,
    Path    = 3,
    Arc     = 4,
    Text    = 5
};

class FrameBuffer
{
public:
    /// Replace the contents with the encoding of `frame` (buffers are reused between frames).
    void Encode(const Frame &frame);

    [[nodiscard]] const std::vector<double> &Data() const { return _data; }
    [[nodiscard]] const std::string         &Strings() const { return _strings; }

private:
    void _Color(Color c);
    void _Stroke(const Stroke &s);
    void _Path(const Path &p);
    void _String(const std::string &s);
    void _Item(const Item &it);

    std::vector<double> _data;
    std::string         _strings;
};

} // namespace ad

#endif // _ENGINE_FRAME_BUFFER_HPP_
