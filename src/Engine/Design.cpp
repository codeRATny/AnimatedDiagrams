#include "Design.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include "Engine/Scene.hpp"
#include "Utils/I18n.hpp"

namespace ad
{

void ApplyDesignSystem(Model &m, const DesignSystem &ds)
{
    m.design_system = ds.id;
    auto &scene     = m.scene;
    // tokens are allowed in the design system; the scene stores plain colors
    auto color = [&ds](const std::optional<std::string> &v, std::string &dst)
    {
        if (v.has_value())
        {
            if (std::string c = ResolveColorToken(*v, &ds); !c.empty())
            {
                dst = std::move(c);
            }
        }
    };
    color(ds.background, scene.background);
    color(ds.edge_color, scene.edge_color);
    color(ds.text_color, scene.text_color);
    scene.grid      = ds.grid.value_or(scene.grid);
    scene.grid_size = ds.grid_size.value_or(scene.grid_size);
}

void ClearDesignSystem(Model &m) { m.design_system.clear(); }

DesignSystem DesignFromScene(const Model &m, const DesignSystem *base, std::string id, std::string label)
{
    DesignSystem ds = base != nullptr ? *base : DesignSystem{};
    ds.id           = std::move(id);
    ds.label        = std::move(label);
    ds.category     = Tr("library", "Mine");
    ds.background   = m.scene.background;
    ds.edge_color   = m.scene.edge_color;
    ds.text_color   = m.scene.text_color;
    ds.grid         = m.scene.grid;
    ds.grid_size    = m.scene.grid_size;
    if (ds.colors.empty())
    {
        ds.colors = DefaultTokens();
    }
    return ds;
}

// ---------------------------------------------------------------------------
// UI palette
// ---------------------------------------------------------------------------

namespace
{

double Channel(uint8_t v)
{
    const double c = v / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double Luminance(Color c) { return 0.2126 * Channel(c.r) + 0.7152 * Channel(c.g) + 0.0722 * Channel(c.b); }

/// `fg` moved towards black / white until it reaches `ratio` against `bg` and `bg2`.
Color Readable(Color fg, Color bg, double ratio, std::optional<Color> bg2 = std::nullopt)
{
    const Color black = Color::Rgb(0x000000);
    const Color white = Color::Rgb(0xffffff);
    auto        worst = [&](Color c)
    {
        return std::min(ContrastRatio(c, bg), bg2.has_value() ? ContrastRatio(c, *bg2) : 21.0);
    };
    auto ok = [&](Color c)
    {
        return worst(c) >= ratio;
    };
    const Color target = worst(black) > worst(white) ? black : white;
    for (int i = 1; i <= 6 && !ok(fg); ++i)
    {
        fg = fg.Mix(target, 0.2 * i);
    }
    return ok(fg) ? fg : target;
}

/// Background `c` pushed away from the text color (black for light UIs, white for dark ones)
/// until pure text reaches `ratio` on it: mid-tone backgrounds become usable surfaces.
Color Surface(Color c, bool light, double ratio)
{
    const Color text = light ? Color::Rgb(0x000000) : Color::Rgb(0xffffff);
    const Color away = light ? Color::Rgb(0xffffff) : Color::Rgb(0x000000);
    for (int i = 1; i <= 8 && ContrastRatio(text, c) < ratio; ++i)
    {
        c = c.Mix(away, 0.15 * i);
    }
    return c;
}

} // namespace

double ContrastRatio(Color a, Color b)
{
    const double la = Luminance(a);
    const double lb = Luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

UiPalette DeriveUiPalette(const DesignSystem *ds)
{
    auto token = [ds](const char *name)
    {
        const std::string v = ResolveColorToken(std::string("$") + name, ds);
        return Color::Parse(v, Color::Parse(DefaultTokens().at(name), Color{}));
    };
    const Color bg = ds != nullptr && ds->background.has_value() ? Color::Parse(ResolveColorToken(*ds->background, ds), palette::kCanvasBg)
                                                                 : palette::kCanvasBg;
    UiPalette   p;
    p.light = ContrastRatio(Color::Rgb(0x000000), bg) > ContrastRatio(Color::Rgb(0xffffff), bg); // dark text reads better
    // surfaces: lifted from the background towards the design system surface and text
    const Color text = token("text");
    p.base           = Surface(bg, p.light, 12.0);
    p.panel          = Surface(p.base.Mix(token("surface"), 0.22).Mix(text, 0.03), p.light, 11.0);
    p.window         = p.base.Mix(p.panel, 0.5);
    p.hover          = p.panel.Mix(text, 0.07);
    p.border         = p.base.Mix(token("border"), p.light ? 0.75 : 0.45);
    p.text           = Readable(text, p.panel, 7.0, p.base);
    p.muted          = Readable(token("muted").Mix(p.base, 0.3), p.window, 4.5);
    p.faint          = p.muted.Mix(p.window, 0.35);
    p.accent         = token("primary");
    const Color dark = Color::Rgb(0x0b1220);
    p.accent_text    = ContrastRatio(dark, p.accent) > ContrastRatio(Color::Rgb(0xffffff), p.accent) ? dark : Color::Rgb(0xffffff);
    p.selected       = p.panel.Mix(p.accent, p.light ? 0.18 : 0.3);
    p.danger         = Readable(token("danger"), p.window, 3.0);
    p.warning        = Readable(token("warning"), p.window, 3.0);
    p.success        = Readable(token("success"), p.window, 3.0);
    return p;
}

} // namespace ad
