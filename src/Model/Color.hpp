#ifndef _MODEL_COLOR_HPP_
#define _MODEL_COLOR_HPP_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

/// @file Color.hpp
/// @brief 24-bit RGB color with "#rrggbb" parsing and d3-compatible darkening.

namespace ad
{

struct Color
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    static constexpr Color Rgb(uint32_t hex)
    {
        return {static_cast<uint8_t>((hex >> 16U) & 0xffU), static_cast<uint8_t>((hex >> 8U) & 0xffU), static_cast<uint8_t>(hex & 0xffU)};
    }
    /// "#rrggbb" or "#rgb" (case-insensitive, surrounding spaces allowed).
    static std::optional<Color> Parse(std::string_view s);
    /// Parse() or the fallback color.
    static Color Parse(std::string_view s, Color fallback);

    [[nodiscard]] std::string Hex() const; // "#rrggbb"
    /// Darken like d3.color().darker(k): channels * 0.7^k.
    [[nodiscard]] Color Darker(double k = 1) const;
    /// Linear blend: t = 0 gives *this, t = 1 gives `other`.
    [[nodiscard]] Color Mix(Color other, double t) const;
    /// Perceived lightness in [0, 255].
    [[nodiscard]] int Lightness() const;

    friend constexpr bool operator==(Color, Color) = default;
};

} // namespace ad

#endif // _MODEL_COLOR_HPP_
