#include <gtest/gtest.h>

#include "Engine/Effects.hpp"
#include "Model/Library.hpp"

using namespace ad;

namespace
{

EffectDef Fade()
{
    EffectDef e;
    e.id     = "fade";
    e.repeat = 1;
    e.tracks = {{EffectProperty::Opacity, {{0, 1}, {1, 0}}}};
    return e;
}

} // namespace

TEST(EffectsTest, TrackInterpolationWithEasing)
{
    EffectTrack t{EffectProperty::Scale, {{0, 1}, {0.5, 2, Easing::EaseIn}, {1, 1}}};
    EXPECT_DOUBLE_EQ(EvaluateTrack(t, 0), 1);
    EXPECT_DOUBLE_EQ(EvaluateTrack(t, 0.25), 1.25); // ease-in at u=0.5 -> 0.25
    EXPECT_DOUBLE_EQ(EvaluateTrack(t, 0.5), 2);
    EXPECT_DOUBLE_EQ(EvaluateTrack(t, 0.75), 1.5);
    EXPECT_DOUBLE_EQ(EvaluateTrack(t, 2), 1);
    EXPECT_DOUBLE_EQ(EvaluateTrack({EffectProperty::Glow, {}}, 0.5), 0);
    EffectTrack unsorted{EffectProperty::Rotate, {{1, 90}, {0, 0}}};
    EXPECT_DOUBLE_EQ(EvaluateTrack(unsorted, 0.5), 45);
}

TEST(EffectsTest, RepeatAndEnd)
{
    EffectDef e = Fade();
    e.repeat    = 2;
    EXPECT_DOUBLE_EQ(EvaluateEffect(e, 0.25).opacity, 0.5);
    EXPECT_DOUBLE_EQ(EvaluateEffect(e, 0.75).opacity, 0.5);
    EXPECT_DOUBLE_EQ(EvaluateEffect(e, 1.0).opacity, 0.0); // end of the last repetition
    EXPECT_DOUBLE_EQ(EvaluateEffect(e, 0.25, 1).opacity, 0.75);
}

TEST(EffectsTest, IntensityAndColor)
{
    const Color red = Color::Rgb(0xff0000);
    const auto  st  = EvaluateEffect(Fade(), 1.0, 0, 0.5, &red);
    EXPECT_DOUBLE_EQ(st.opacity, 0.5);
    EXPECT_EQ(st.color, red);
}

TEST(EffectsTest, Combine)
{
    EffectState a;
    a.scale = 1.1;
    a.glow  = 0.3;
    EffectState b;
    b.scale = 2;
    b.dx    = 5;
    b.glow  = 0.8;
    b.color = Color::Rgb(0x00ff00);
    a.Combine(b);
    EXPECT_DOUBLE_EQ(a.scale, 2.2);
    EXPECT_DOUBLE_EQ(a.dx, 5);
    EXPECT_DOUBLE_EQ(a.glow, 0.8);
    EXPECT_EQ(a.color, Color::Rgb(0x00ff00));
    EXPECT_TRUE(EffectState{}.IsIdentity());
    EXPECT_FALSE(a.IsIdentity());
}

TEST(EffectsTest, BuiltinsStayInRange)
{
    for (const auto &e : BuiltinLibrary().effects)
    {
        for (int i = 0; i <= 20; ++i)
        {
            const auto st = EvaluateEffect(e, i / 20.0);
            EXPECT_GE(st.opacity, 0) << e.id;
            EXPECT_LE(st.opacity, 1) << e.id;
            EXPECT_GE(st.glow, 0) << e.id;
            EXPECT_LE(st.tint, 1) << e.id;
        }
    }
}
