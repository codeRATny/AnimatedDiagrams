#include <gtest/gtest.h>

#include <algorithm>

#include "Engine/Scene.hpp"
#include "Model/Sample.hpp"

using namespace ad;

namespace
{

std::vector<std::string> Texts(const Frame &f)
{
    std::vector<std::string> out;
    for (const auto &it : f.items)
    {
        if (const auto *t = std::get_if<TextShape>(&it.shape); t != nullptr)
        {
            out.push_back(t->text);
        }
    }
    return out;
}

bool HasText(const Frame &f, std::string_view s) { return std::ranges::contains(Texts(f), std::string(s)); }

const ApproxTextMeasurer kTm;

} // namespace

TEST(SceneTest, Utf8Length)
{
    EXPECT_EQ(Utf8Length("abc"), 3U);
    EXPECT_EQ(Utf8Length("абв"), 3U);
    EXPECT_EQ(Utf8Length("⏱x"), 2U);
}

TEST(SceneTest, StaticDiagram)
{
    const Frame f = BuildFrame(SampleModel(), 0, {}, kTm);
    EXPECT_TRUE(HasText(f, "Сервис A"));
    EXPECT_TRUE(HasText(f, "REST"));
    EXPECT_TRUE(HasText(f, "Норма"));
    EXPECT_FALSE(HasText(f, "запрос"));
}

TEST(SceneTest, MessagesStatesTimer)
{
    const Model m = SampleModel();
    EXPECT_TRUE(HasText(BuildFrame(m, 800, {}, kTm), "запрос"));
    EXPECT_TRUE(HasText(BuildFrame(m, 2000, {}, kTm), "Недоступен"));
    EXPECT_TRUE(HasText(BuildFrame(m, 1600, {}, kTm), "5с"));
    EXPECT_TRUE(HasText(BuildFrame(m, 4100, {}, kTm), "3с"));
    const Frame last = BuildFrame(m, m.scenario.duration, {}, kTm);
    EXPECT_TRUE(HasText(last, "Успех"));
    EXPECT_TRUE(HasText(last, "Активен"));
}

TEST(SceneTest, EffectTransformsNode)
{
    const Model m = SampleModel(); // pulse on svcA at [6600, 7300]
    const Frame f = BuildFrame(m, 6750, {}, kTm);
    EXPECT_TRUE(std::ranges::any_of(f.items,
                                    [](const Item &it)
                                    {
                                        return it.transform.has_value() && it.transform->scale > 1 &&
                                               it.transform->origin == Vec2{155, 153};
                                    }));
}

TEST(SceneTest, CustomEffectFromDocumentLibrary)
{
    Model     m = SampleModel();
    EffectDef fx;
    fx.id     = "slide";
    fx.tracks = {{EffectProperty::OffsetX, {{0, 0}, {1, 100}}}};
    m.library.Upsert(fx);
    Step s;
    s.id       = "fx";
    s.type     = StepType::Effect;
    s.node_id  = "svcC";
    s.effect   = "slide";
    s.start    = 0;
    s.duration = 1000;
    m.scenario.steps.push_back(s);
    const Frame f = BuildFrame(m, 500, {}, kTm);
    EXPECT_TRUE(std::ranges::any_of(f.items,
                                    [](const Item &it)
                                    {
                                        return it.transform.has_value() && it.transform->translate.x == 50;
                                    }));
}

TEST(SceneTest, PacketStreamDrawsSeveralPackets)
{
    Model m         = SampleModel();
    Step &s         = *m.FindStep("s1");
    s.packet        = "dot";
    s.packet_count  = 3;
    auto count_dots = [&](double t)
    {
        const Frame f = BuildFrame(m, t, {}, kTm);
        return std::ranges::count_if(f.items,
                                     [](const Item &it)
                                     {
                                         const auto *e = std::get_if<EllipseShape>(&it.shape);
                                         return e != nullptr && e->rx == 7 && it.transform.has_value();
                                     });
    };
    EXPECT_EQ(count_dots(300 + 1100 * 0.6), 3);
}

TEST(SceneTest, WaypointHandlesOnlyWithChrome)
{
    Model m                      = SampleModel();
    m.FindEdge("eAB")->waypoints = {{300, 20}};
    auto handles                 = [&](const SceneOptions &o)
    {
        const Frame f = BuildFrame(m, 0, o, kTm);
        return std::ranges::count_if(f.items,
                                     [](const Item &it)
                                     {
                                         const auto *e = std::get_if<EllipseShape>(&it.shape);
                                         return e != nullptr && e->center == Vec2{300, 20};
                                     });
    };
    SceneOptions sel;
    sel.selection = {Selection::Kind::Edge, "eAB"};
    EXPECT_EQ(handles({}), 0);
    EXPECT_EQ(handles(sel), 1);
    sel.editor_chrome = false;
    EXPECT_EQ(handles(sel), 0);
}

TEST(SceneTest, StylesAreApplied)
{
    Model m                     = SampleModel();
    m.nodes[0].style.fill       = "#102030";
    m.nodes[0].style.text_color = "#ff0000";
    m.edges[0].style.color      = "#00ff00";
    const Frame f               = BuildFrame(m, 0, {}, kTm);
    EXPECT_TRUE(std::ranges::any_of(f.items,
                                    [](const Item &it)
                                    {
                                        return it.paint.fill == Color::Rgb(0x102030);
                                    }));
    EXPECT_TRUE(std::ranges::any_of(f.items,
                                    [](const Item &it)
                                    {
                                        return it.paint.stroke.has_value() && it.paint.stroke->color == Color::Rgb(0x00ff00);
                                    }));
}

TEST(SceneTest, ContentBounds)
{
    Model m                      = SampleModel();
    m.FindEdge("eAB")->waypoints = {{1000, -300}};
    const Rect r                 = ContentBounds(m, kTm);
    EXPECT_LE(r.y, -300);
    EXPECT_GE(r.Right(), 1000);
    EXPECT_EQ(ContentBounds(Model{}, kTm), (Rect{0, 0, 640, 360}));
}
