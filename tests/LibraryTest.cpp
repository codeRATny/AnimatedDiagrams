#include <gtest/gtest.h>

#include <algorithm>
#include <set>

#include "Model/Library.hpp"

using namespace ad;

TEST(LibraryTest, NodeStyleMerge)
{
    NodeStyle base;
    base.shape = "rounded";
    base.fill  = "#111111";
    NodeStyle over;
    over.fill         = "#222222";
    over.font_size    = 18;
    const NodeStyle m = base.Merged(over);
    EXPECT_EQ(m.shape, "rounded");
    EXPECT_EQ(m.fill, "#222222");
    EXPECT_EQ(m.font_size, 18);
    EXPECT_TRUE(NodeStyle{}.Empty());
    EXPECT_FALSE(m.Empty());
}

TEST(LibraryTest, EffectPropertyStrings)
{
    for (const EffectProperty p : EffectProperties())
    {
        EXPECT_EQ(EffectPropertyFromString(ToString(p)), p);
    }
    EXPECT_DOUBLE_EQ(DefaultValue(EffectProperty::Opacity), 1);
    EXPECT_DOUBLE_EQ(DefaultValue(EffectProperty::Glow), 0);
    EXPECT_FALSE(EffectPropertyFromString("blur").has_value());
}

TEST(LibraryTest, UpsertReplacesById)
{
    LibrarySet  set;
    ElementType e;
    e.id    = "x";
    e.label = "one";
    set.Upsert(e);
    e.label = "two";
    set.Upsert(e);
    ASSERT_EQ(set.elements.size(), 1U);
    EXPECT_EQ(set.Element("x")->label, "two");
    EXPECT_EQ(set.Effect("x"), nullptr);
    EXPECT_FALSE(set.Empty());
}

TEST(LibraryTest, BuiltinsAreConsistent)
{
    const LibrarySet     &b = BuiltinLibrary();
    std::set<std::string> ids;
    for (const auto &e : b.elements)
    {
        EXPECT_TRUE(ids.insert(e.id).second) << e.id;
        EXPECT_FALSE(e.label.empty());
    }
    for (const auto &e : b.effects)
    {
        EXPECT_TRUE(ids.insert(e.id).second) << e.id;
        EXPECT_FALSE(e.tracks.empty()) << e.id;
    }
    for (const auto &a : b.animations)
    {
        EXPECT_TRUE(ids.insert(a.id).second) << a.id;
        for (const auto &s : a.steps)
        {
            // every node reference of a template step is one of its roles
            for (const std::string *ref : {&s.from, &s.to, &s.node_id})
            {
                if (!ref->empty())
                {
                    EXPECT_TRUE(std::ranges::any_of(a.roles,
                                                    [&](const AnimationRole &r)
                                                    {
                                                        return r.id == *ref;
                                                    }))
                        << a.id << ": " << *ref;
                }
            }
        }
    }
    EXPECT_NE(b.Element("service"), nullptr);
    EXPECT_NE(b.Effect("pulse"), nullptr);
    EXPECT_NE(b.Animation("request-response"), nullptr);
}
