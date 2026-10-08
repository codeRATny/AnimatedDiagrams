#include <gtest/gtest.h>

#include "Model/Color.hpp"

using namespace ad;

TEST(ColorTest, ParsesLongAndShortHex)
{
    EXPECT_EQ(Color::Parse("#4f8cff"), Color::Rgb(0x4f8cff));
    EXPECT_EQ(Color::Parse("#4F8CFF"), Color::Rgb(0x4f8cff));
    EXPECT_EQ(Color::Parse("#fff"), Color::Rgb(0xffffff));
    EXPECT_EQ(Color::Parse(" #000000 "), Color::Rgb(0));
}

TEST(ColorTest, RejectsInvalid)
{
    EXPECT_FALSE(Color::Parse("").has_value());
    EXPECT_FALSE(Color::Parse("4f8cff").has_value());
    EXPECT_FALSE(Color::Parse("#4f8cf").has_value());
    EXPECT_FALSE(Color::Parse("#zzzzzz").has_value());
    EXPECT_EQ(Color::Parse("bad", Color::Rgb(0x123456)), Color::Rgb(0x123456));
}

TEST(ColorTest, HexDarkerMixLightness)
{
    EXPECT_EQ(Color::Rgb(0x0a111f).Hex(), "#0a111f");
    EXPECT_EQ(Color::Rgb(0xef4444).Darker(1.9), (Color{121, 35, 35})); // d3.color("#ef4444").darker(1.9)
    EXPECT_EQ(Color::Rgb(0x000000).Mix(Color::Rgb(0xffffff), 0.5), (Color{128, 128, 128}));
    EXPECT_EQ(Color::Rgb(0xffffff).Lightness(), 255);
    EXPECT_EQ(Color::Rgb(0x000000).Lightness(), 0);
}
