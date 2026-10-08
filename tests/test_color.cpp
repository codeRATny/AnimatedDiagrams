#include <gtest/gtest.h>

#include "ad/catalog.hpp"
#include "ad/color.hpp"

using namespace ad;

TEST(Color, ParsesLongAndShortHex) {
    EXPECT_EQ(Color::parse("#4f8cff"), Color::rgb(0x4f8cff));
    EXPECT_EQ(Color::parse("#4F8CFF"), Color::rgb(0x4f8cff));
    EXPECT_EQ(Color::parse("#fff"), Color::rgb(0xffffff));
    EXPECT_EQ(Color::parse(" #000000 "), Color::rgb(0));
}

TEST(Color, RejectsInvalid) {
    EXPECT_FALSE(Color::parse(""));
    EXPECT_FALSE(Color::parse("4f8cff"));
    EXPECT_FALSE(Color::parse("#4f8cf"));
    EXPECT_FALSE(Color::parse("#zzzzzz"));
    EXPECT_FALSE(Color::parse("red"));
    EXPECT_EQ(Color::parseOr("bad", Color::rgb(0x123456)), Color::rgb(0x123456));
}

TEST(Color, HexRoundTrip) { EXPECT_EQ(Color::rgb(0x0a111f).hex(), "#0a111f"); }

TEST(Color, DarkerMatchesD3) {
    // d3.color("#ef4444").darker(1.9) → rgb(121, 35, 35)
    EXPECT_EQ(Color::rgb(0xef4444).darker(1.9), (Color{121, 35, 35}));
    EXPECT_EQ(Color::rgb(0xffffff).darker(0), Color::rgb(0xffffff));
}

TEST(Color, Mix) {
    EXPECT_EQ(Color::rgb(0x000000).mix(Color::rgb(0xffffff), 0.5), (Color{128, 128, 128}));
    EXPECT_EQ(Color::rgb(0x102030).mix(Color::rgb(0xffffff), 0), Color::rgb(0x102030));
}

TEST(Catalog, LookupWithFallback) {
    EXPECT_EQ(nodeKind("db").shape, "db");
    EXPECT_EQ(nodeKind("unknown").id, "service");
    EXPECT_EQ(nodeState("down").label, "Недоступен");
    EXPECT_EQ(msgVariant("retry").dash, 6);
    EXPECT_EQ(linkAnim("nope").id, "flow");
    EXPECT_EQ(timeUnit("m").shortLabel, "мин");
}

TEST(Catalog, StepTypeStrings) {
    for (const auto& info : stepTypes()) {
        EXPECT_EQ(toString(info.type), info.id);
        EXPECT_EQ(stepTypeFromString(info.id), info.type);
    }
    EXPECT_FALSE(stepTypeFromString("bogus"));
}
