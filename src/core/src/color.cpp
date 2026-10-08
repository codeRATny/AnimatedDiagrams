#include "ad/color.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace ad {

namespace {

std::optional<int> hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return std::nullopt;
}

std::uint8_t clampByte(double v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L)); }

}  // namespace

std::optional<Color> Color::parse(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    if (s.empty() || s.front() != '#') return std::nullopt;
    s.remove_prefix(1);
    if (s.size() != 3 && s.size() != 6) return std::nullopt;
    int v[6] = {};
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto d = hexDigit(s[i]);
        if (!d) return std::nullopt;
        v[i] = *d;
    }
    if (s.size() == 3)
        return Color{static_cast<std::uint8_t>(v[0] * 17), static_cast<std::uint8_t>(v[1] * 17),
                     static_cast<std::uint8_t>(v[2] * 17)};
    return Color{static_cast<std::uint8_t>(v[0] * 16 + v[1]), static_cast<std::uint8_t>(v[2] * 16 + v[3]),
                 static_cast<std::uint8_t>(v[4] * 16 + v[5])};
}

Color Color::parseOr(std::string_view s, Color fallback) { return parse(s).value_or(fallback); }

std::string Color::hex() const { return std::format("#{:02x}{:02x}{:02x}", r, g, b); }

Color Color::darker(double k) const {
    const double f = std::pow(0.7, k);
    return {clampByte(r * f), clampByte(g * f), clampByte(b * f)};
}

Color Color::mix(Color o, double t) const {
    return {clampByte(r + (o.r - r) * t), clampByte(g + (o.g - g) * t), clampByte(b + (o.b - b) * t)};
}

}  // namespace ad
