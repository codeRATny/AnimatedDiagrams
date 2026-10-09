#include <gtest/gtest.h>

#include <algorithm>

#include "Engine/Design.hpp"
#include "Engine/Engine.hpp"
#include "Engine/Scene.hpp"
#include "Io/JsonCodec.hpp"
#include "Io/JsonIo.hpp"
#include "Model/Registry.hpp"
#include "Model/Sample.hpp"
#include "Plugins/Plugin.hpp"

using namespace ad;

namespace
{

const ApproxTextMeasurer kTm;

Model TwoNodes()
{
    Model m = EmptyModel();
    Node  a;
    a.id    = "a";
    a.label = "A";
    a.type  = "db";
    Node b  = a;
    b.id    = "b";
    b.x     = 300;
    b.type  = "service";
    m.nodes = {a, b};
    Edge e;
    e.id    = "e";
    e.from  = "a";
    e.to    = "b";
    m.edges = {e};
    return m;
}

const PathShape *NodeBody(const Frame &f)
{
    for (const auto &it : f.items)
    {
        if (const auto *p = std::get_if<PathShape>(&it.shape); p != nullptr && it.paint.fill.has_value() && it.paint.stroke.has_value())
        {
            return p; // the first filled + stroked path is the first node body
        }
    }
    return nullptr;
}

} // namespace

TEST(DesignSystemTest, BuiltinsAndTokens)
{
    const Registry &reg = Registry::Default();
    for (const char *id : {"dark", "light", "blueprint", "high-contrast"})
    {
        EXPECT_NE(reg.FindDesignSystem(id), nullptr) << id;
    }
    const DesignSystem *light = reg.FindDesignSystem("light");
    EXPECT_EQ(ResolveColorToken("$primary", light), "#2563eb");
    EXPECT_EQ(ResolveColorToken("$primary", nullptr), DefaultTokens().at("primary"));
    EXPECT_EQ(ResolveColorToken("$nope", light), "");
    EXPECT_EQ(ResolveColorToken("#123456", light), "#123456");
}

TEST(DesignSystemTest, ApplyCopiesCanvasTokens)
{
    Model m = TwoNodes();
    ApplyDesignSystem(m, *Registry::Default().FindDesignSystem("light"));
    EXPECT_EQ(m.design_system, "light");
    EXPECT_EQ(m.scene.background, "#f6f8fc");
    EXPECT_EQ(m.scene.text_color, "#0f172a");
    ClearDesignSystem(m);
    EXPECT_TRUE(m.design_system.empty());
    EXPECT_EQ(m.scene.background, "#f6f8fc"); // the scene keeps the colors

    const DesignSystem captured = DesignFromScene(m, nullptr, "mine", "Mine");
    EXPECT_EQ(captured.background, "#f6f8fc");
    EXPECT_FALSE(captured.colors.empty());
}

TEST(DesignSystemTest, StylePrecedence)
{
    Registry     reg;
    LibrarySet   plugin;
    DesignSystem ds;
    ds.id                        = "t";
    ds.node.corner_radius        = 3;
    ds.node.fill                 = "$surface";
    ds.elements["db"].style.fill = "#111111";
    ds.elements["db"].accent     = "$danger";
    ds.edge.routing              = "orthogonal";
    ds.edge.width                = 5;
    ds.colors["surface"]         = "#abcdef";
    ds.states["down"]            = {"#220000", "$danger"};
    plugin.design_systems.push_back(ds);
    reg.AddSource("p", plugin);

    Model m                        = TwoNodes();
    m.design_system                = "t";
    m.nodes[0].style.corner_radius = 9;

    const NodeStyle db = ResolveNodeStyle(m, m.nodes[0], reg);
    EXPECT_EQ(db.shape, "cylinder"); // element type
    EXPECT_EQ(db.fill, "#111111");   // design system element override
    EXPECT_EQ(db.corner_radius, 9);  // node wins
    const NodeStyle svc = ResolveNodeStyle(m, m.nodes[1], reg);
    EXPECT_EQ(svc.fill, "$surface"); // design system base (token resolved later)
    EXPECT_EQ(svc.corner_radius, 3);
    EXPECT_EQ(ResolveAccent(m, m.nodes[0], reg).Hex(), DefaultTokens().at("danger"));

    const ResolvedState ok = ResolveNodeState(nullptr, svc, &ds);
    EXPECT_EQ(ok.fill.Hex(), "#abcdef");
    Step down;
    down.state              = "down";
    const ResolvedState dst = ResolveNodeState(&down, {}, &ds);
    EXPECT_EQ(dst.fill.Hex(), "#220000");
    EXPECT_EQ(dst.ring.Hex(), DefaultTokens().at("danger"));

    m.edges[0].style.width = 1;
    const EdgeStyle es     = ResolveEdgeStyle(m, m.edges[0], reg);
    EXPECT_EQ(es.routing, "orthogonal");
    EXPECT_EQ(es.width, 1);
}

TEST(DesignSystemTest, SceneUsesDesignSystem)
{
    const Registry &reg = Registry::Default();
    Model           m   = TwoNodes();
    ApplyDesignSystem(m, *reg.FindDesignSystem("light"));
    const Frame      f    = BuildFrame(m, 0, {}, kTm, reg);
    const PathShape *body = NodeBody(f);
    ASSERT_NE(body, nullptr);
    bool white_fill = false;
    bool mono       = false;
    for (const auto &it : f.items)
    {
        white_fill = white_fill || (std::holds_alternative<PathShape>(it.shape) && it.paint.fill == Color::Rgb(0xffffff));
    }
    EXPECT_TRUE(white_fill);

    ApplyDesignSystem(m, *reg.FindDesignSystem("blueprint"));
    for (const auto &it : BuildFrame(m, 0, {}, kTm, reg).items)
    {
        if (const auto *t = std::get_if<TextShape>(&it.shape); t != nullptr)
        {
            mono = mono || t->font.family == "DejaVu Sans Mono";
        }
    }
    EXPECT_TRUE(mono);
}

TEST(DesignSystemTest, JsonRoundTripAndPlugins)
{
    Model m = TwoNodes();
    ApplyDesignSystem(m, *Registry::Default().FindDesignSystem("blueprint"));
    DesignSystem custom          = *Registry::Default().FindDesignSystem("light");
    custom.id                    = "custom";
    custom.elements["db"].accent = "$secondary";
    m.library.Upsert(custom);
    const auto parsed = ParseModel(SerializeModel(m));
    ASSERT_TRUE(parsed.has_value()) << parsed.error();
    EXPECT_EQ(parsed->design_system, "blueprint");
    ASSERT_NE(parsed->library.Design("custom"), nullptr);
    EXPECT_EQ(*parsed->library.Design("custom"), custom);

    const auto plugin = ParsePlugin(R"({"format": "animated-diagrams-plugin", "formatVersion": 1, "id": "x.ds", "name": "DS",
        "designSystems": [{"id": "sunset", "label": "Sunset", "background": "#2a1020", "colors": {"primary": "#ff7a59"},
                           "states": {"active": {"fill": "#4a1d2e", "ring": "$primary"}}, "fontFamily": "Serif"}]})");
    ASSERT_TRUE(plugin.has_value()) << plugin.error();
    ASSERT_EQ(plugin->library.design_systems.size(), 1U);
    EXPECT_EQ(plugin->library.design_systems[0].font_family, "Serif");
    EXPECT_EQ(MakePlugin(plugin->info, plugin->library, {"sunset"}).library.design_systems.size(), 1U);

    Registry reg;
    reg.AddSource("x.ds", plugin->library);
    Model doc         = TwoNodes();
    doc.design_system = "sunset";
    EXPECT_EQ(reg.EmbedUsedDefinitions(doc), 1);
    EXPECT_NE(doc.library.Design("sunset"), nullptr);
}
