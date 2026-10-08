#include "Color.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace ad
{

namespace
{

std::optional<int> HexDigit(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return std::nullopt;
}

uint8_t ClampByte(double v) { return static_cast<uint8_t>(std::clamp(std::round(v), 0.0, 255.0)); }

} // namespace

std::optional<Color> Color::Parse(std::string_view s)
{
    while (!s.empty() && s.front() == ' ')
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && s.back() == ' ')
    {
        s.remove_suffix(1);
    }
    if (s.empty() || s.front() != '#')
    {
        return std::nullopt;
    }
    s.remove_prefix(1);
    if (s.size() != 3 && s.size() != 6)
    {
        return std::nullopt;
    }
    int v[6]{};
    for (size_t i = 0; i < s.size(); ++i)
    {
        const auto d = HexDigit(s[i]);
        if (!d.has_value())
        {
            return std::nullopt;
        }
        v[i] = *d;
    }
    if (s.size() == 3)
    {
        return Color{static_cast<uint8_t>(v[0] * 17), static_cast<uint8_t>(v[1] * 17), static_cast<uint8_t>(v[2] * 17)};
    }
    return Color{static_cast<uint8_t>(v[0] * 16 + v[1]), static_cast<uint8_t>(v[2] * 16 + v[3]), static_cast<uint8_t>(v[4] * 16 + v[5])};
}

Color Color::Parse(std::string_view s, Color fallback) { return Parse(s).value_or(fallback); }

std::string Color::Hex() const { return std::format("#{:02x}{:02x}{:02x}", r, g, b); }

Color Color::Darker(double k) const
{
    const double f = std::pow(0.7, k);
    return {ClampByte(r * f), ClampByte(g * f), ClampByte(b * f)};
}

Color Color::Mix(Color o, double t) const
{
    return {ClampByte(r + (o.r - r) * t), ClampByte(g + (o.g - g) * t), ClampByte(b + (o.b - b) * t)};
}

int Color::Lightness() const { return static_cast<int>(std::lround(0.299 * r + 0.587 * g + 0.114 * b)); }

} // namespace ad
