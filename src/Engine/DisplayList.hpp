#ifndef _ENGINE_DISPLAY_LIST_HPP_
#define _ENGINE_DISPLAY_LIST_HPP_

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "Geometry/Path.hpp"
#include "Model/Color.hpp"

/// @file DisplayList.hpp
/// @brief Renderer-independent frame description: primitives in world coordinates.
///        The same frame is drawn on screen and into exported GIF / video frames.

namespace ad
{

struct Font
{
    double      size = 12; // px in world units
    bool        bold = false;
    std::string family; // empty -- the default UI font

    Font() = default;
    Font(double s, bool b, std::string f = {}) : size(s), bold(b), family(std::move(f)) {}
    friend bool operator==(const Font &, const Font &) = default;
};

/// Text width measurement, implemented by the renderer (Qt) or approximated (tests, headless).
class TextMeasurer
{
public:
    virtual ~TextMeasurer()                                                           = default;
    [[nodiscard]] virtual double Width(std::string_view utf8, const Font &font) const = 0;
};

/// 0.6 * size per character -- no font dependency.
class ApproxTextMeasurer final : public TextMeasurer
{
public:
    [[nodiscard]] double Width(std::string_view utf8, const Font &font) const override;
};

struct Stroke
{
    Color               color;
    double              width = 1;
    std::vector<double> dash; // empty -- solid
    double              dash_offset = 0;
    bool                round_cap   = false;
};

enum class PaintEffect
{
    None,
    Shadow,
    Glow
};

struct Paint
{
    std::optional<Color>  fill;
    std::optional<Stroke> stroke;
    double                opacity = 1;
    PaintEffect           effect  = PaintEffect::None;
};

/// p' = origin + R(rotate_deg) * S(scale) * (p - origin) + translate
struct Transform
{
    Vec2   origin;
    double rotate_deg = 0;
    double scale      = 1;
    Vec2   translate;
};

enum class HAlign
{
    Left,
    Center,
    Right
};

enum class VAlign
{
    Baseline,
    Middle
};

struct RectShape
{
    Rect   rect;
    double radius = 0;
};

struct EllipseShape
{
    Vec2   center;
    double rx = 0;
    double ry = 0;
};

struct PathShape
{
    Path path;
};

/// Circular arc; angles in degrees, 0 = +x, positive is clockwise on screen.
struct ArcShape
{
    Vec2   center;
    double radius    = 0;
    double start_deg = 0;
    double sweep_deg = 360;
};

struct TextShape
{
    Vec2                  pos;
    std::string           text;
    Font                  font;
    HAlign                align  = HAlign::Left;
    VAlign                valign = VAlign::Baseline;
    std::optional<Stroke> halo; // outline under the text (readability over lines)
};

using Shape = std::variant<RectShape, EllipseShape, PathShape, ArcShape, TextShape>;

struct Item
{
    Shape                    shape;
    Paint                    paint;
    std::optional<Transform> transform;
    std::optional<Path>      clip; // clip outline in item coordinates
};

struct Frame
{
    std::vector<Item> items; // in drawing order
};

size_t Utf8Length(std::string_view s);

} // namespace ad

#endif // _ENGINE_DISPLAY_LIST_HPP_
