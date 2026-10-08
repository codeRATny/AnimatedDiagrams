#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ad {

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    static constexpr Color rgb(std::uint32_t hex) {
        return {static_cast<std::uint8_t>((hex >> 16) & 0xff), static_cast<std::uint8_t>((hex >> 8) & 0xff),
                static_cast<std::uint8_t>(hex & 0xff)};
    }
    /// "#rrggbb" или "#rgb" (регистр не важен).
    static std::optional<Color> parse(std::string_view s);
    /// parse() или запасной цвет.
    static Color parseOr(std::string_view s, Color fallback);

    [[nodiscard]] std::string hex() const;  // "#rrggbb"
    /// Затемнение как в d3.color().darker(k): каналы × 0.7^k.
    [[nodiscard]] Color darker(double k = 1) const;
    /// Линейное смешивание: t=0 → *this, t=1 → other.
    [[nodiscard]] Color mix(Color other, double t) const;

    friend constexpr bool operator==(Color, Color) = default;
};

}  // namespace ad
