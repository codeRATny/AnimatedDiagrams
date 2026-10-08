#include <gtest/gtest.h>

#include <algorithm>

#include "Model/Model.hpp"
#include "Model/Registry.hpp"

using namespace ad;

namespace
{

LibrarySet SetWithElement(const std::string &id, const std::string &label)
{
    LibrarySet  s;
    ElementType e;
    e.id    = id;
    e.label = label;
    s.Upsert(e);
    return s;
}

} // namespace

TEST(RegistryTest, BuiltinsAndFallback)
{
    const Registry reg;
    EXPECT_NE(reg.FindElement("db"), nullptr);
    EXPECT_EQ(reg.FindElement("missing"), nullptr);
    EXPECT_EQ(reg.Element("missing").id, "service");
    EXPECT_EQ(reg.Sources().size(), 1U);
}

TEST(RegistryTest, LookupOrderDocumentPluginBuiltin)
{
    Registry reg;
    reg.AddSource("p1", SetWithElement("service", "from plugin"));
    EXPECT_EQ(reg.FindElement("service")->label, "from plugin");
    const LibrarySet doc = SetWithElement("service", "from document");
    EXPECT_EQ(reg.FindElement("service", &doc)->label, "from document");
    reg.RemoveSource("p1");
    EXPECT_EQ(reg.FindElement("service")->label, "Сервис");
    reg.RemoveSource(kBuiltinSource); // ignored
    EXPECT_NE(reg.FindElement("service"), nullptr);
}

TEST(RegistryTest, ListingHasUniqueIdsWithWinningSource)
{
    Registry reg;
    reg.AddSource("p1", SetWithElement("service", "override"));
    reg.AddSource("p2", SetWithElement("k8s-pod", "Pod"));
    const LibrarySet doc     = SetWithElement("mine", "Mine");
    const auto       entries = reg.Elements(&doc);
    auto             find    = [&](const std::string &id)
    {
        return std::ranges::find_if(entries,
                                    [&](const auto &e)
                                    {
                                        return e.def->id == id;
                                    });
    };
    EXPECT_EQ(std::ranges::count_if(entries,
                                    [](const auto &e)
                                    {
                                        return e.def->id == "service";
                                    }),
              1);
    EXPECT_EQ(find("service")->source, "p1");
    EXPECT_EQ(find("k8s-pod")->source, "p2");
    EXPECT_EQ(find("mine")->source, kDocumentSource);
    EXPECT_EQ(entries.back().def->id, "mine"); // document entries come last
    reg.ClearPlugins();
    EXPECT_EQ(reg.Sources().size(), 1U);
}

TEST(RegistryTest, EmbedUsedDefinitions)
{
    Registry   reg;
    LibrarySet plugin = SetWithElement("k8s-pod", "Pod");
    EffectDef  fx;
    fx.id = "sparkle";
    plugin.Upsert(fx);
    reg.AddSource("k8s", plugin);

    Model m;
    Node  n;
    n.id   = "a";
    n.type = "k8s-pod";
    Node b;
    b.id    = "b";
    b.type  = "service"; // built-in: not embedded
    m.nodes = {n, b};
    Step s;
    s.type    = StepType::Effect;
    s.node_id = "a";
    s.effect  = "sparkle";
    m.scenario.steps.push_back(s);

    EXPECT_EQ(reg.EmbedUsedDefinitions(m), 2);
    EXPECT_NE(m.library.Element("k8s-pod"), nullptr);
    EXPECT_NE(m.library.Effect("sparkle"), nullptr);
    EXPECT_EQ(m.library.Element("service"), nullptr);
    EXPECT_EQ(reg.EmbedUsedDefinitions(m), 0); // idempotent
}
