#include "Design.hpp"

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
    ds.category     = "Мои";
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

} // namespace ad
