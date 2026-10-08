#include <gtest/gtest.h>

#include "Model/Easing.hpp"

using namespace ad;

TEST(EasingTest, EndpointsAreFixed)
{
    for (const auto &info : Easings())
    {
        EXPECT_NEAR(ApplyEasing(info.easing, 0), info.easing == Easing::Step ? 0.0 : 0.0, 1e-9) << info.id;
        EXPECT_NEAR(ApplyEasing(info.easing, 1), 1.0, 1e-9) << info.id;
    }
}

TEST(EasingTest, KnownValues)
{
    EXPECT_DOUBLE_EQ(ApplyEasing(Easing::Linear, 0.3), 0.3);
    EXPECT_DOUBLE_EQ(ApplyEasing(Easing::EaseIn, 0.5), 0.25);
    EXPECT_DOUBLE_EQ(ApplyEasing(Easing::EaseOut, 0.5), 0.75);
    EXPECT_DOUBLE_EQ(ApplyEasing(Easing::EaseInOut, 0.5), 0.5);
    EXPECT_DOUBLE_EQ(ApplyEasing(Easing::Step, 0.99), 0);
    EXPECT_GT(ApplyEasing(Easing::BackOut, 0.7), 1.0);   // overshoots
    EXPECT_DOUBLE_EQ(ApplyEasing(Easing::Linear, 2), 1); // clamped input
}

TEST(EasingTest, StringRoundTrip)
{
    for (const auto &info : Easings())
    {
        EXPECT_EQ(ToString(info.easing), info.id);
        EXPECT_EQ(EasingFromString(info.id), info.easing);
    }
    EXPECT_FALSE(EasingFromString("wobbly").has_value());
}
