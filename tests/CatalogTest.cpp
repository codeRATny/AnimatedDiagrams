#include <gtest/gtest.h>

#include "Model/Catalog.hpp"

using namespace ad;

TEST(CatalogTest, LookupWithFallback)
{
    EXPECT_EQ(NodeState("down").label, "Down");
    EXPECT_EQ(NodeState("nope").id, "ok");
    EXPECT_EQ(MsgVariant("retry").dash, 6);
    EXPECT_EQ(LinkAnim("nope").id, "flow");
    EXPECT_EQ(TimeUnit("m").short_label, "min");
}

TEST(CatalogTest, StepTypeStrings)
{
    for (const auto &info : StepTypes())
    {
        EXPECT_EQ(ToString(info.type), info.id);
        EXPECT_EQ(StepTypeFromString(info.id), info.type);
    }
    EXPECT_EQ(StepTypeFromString("pulse"), StepType::Effect); // legacy files
    EXPECT_FALSE(StepTypeFromString("bogus").has_value());
}

TEST(CatalogTest, Options)
{
    EXPECT_TRUE(IsKnownOption(Shapes(), "cylinder"));
    EXPECT_TRUE(IsKnownOption(ArrowHeads(), "none"));
    EXPECT_TRUE(IsKnownOption(PacketShapes(), "envelope"));
    EXPECT_TRUE(IsKnownOption(Routings(), "orthogonal"));
    EXPECT_FALSE(IsKnownOption(Shapes(), "star"));
}
