#include <gtest/gtest.h>

#include "Common/Exceptions.hpp"
#include "Import/XmlReader.hpp"

using namespace ad;

TEST(XmlReaderTest, ElementsAttributesText)
{
    const XmlNode root = ParseXml(R"(<?xml version="1.0" encoding="UTF-8"?>
<!-- comment -->
<!DOCTYPE mxfile>
<mxfile host="app.diagrams.net"><diagram name='Page &amp; 1' id="x">Tm9k&lt;</diagram>
  <empty/><cdata><![CDATA[a < b]]></cdata></mxfile>)");
    EXPECT_EQ(root.name, "mxfile");
    EXPECT_EQ(root.AttrOr("host"), "app.diagrams.net");
    EXPECT_EQ(root.Attr("missing"), nullptr);
    const XmlNode *d = root.Child("diagram");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->AttrOr("name"), "Page & 1");
    EXPECT_EQ(d->text, "Tm9k<");
    EXPECT_EQ(root.Child("cdata")->text, "a < b");
    EXPECT_EQ(root.ChildrenNamed("empty").size(), 1U);
}

TEST(XmlReaderTest, Errors)
{
    EXPECT_THROW(ParseXml(""), ParseError);
    EXPECT_THROW(ParseXml("<a><b></a>"), ParseError);
    EXPECT_THROW(ParseXml("<a x=1/>"), ParseError);
    EXPECT_THROW(ParseXml("<a></a><b/>"), ParseError);
    EXPECT_THROW(ParseXml("<a>"), ParseError);
}

TEST(XmlReaderTest, NumericEntitiesAndUtf8)
{
    const XmlNode n = ParseXml(R"(<a v="&#1057;&#x435;&#1088;&#1074;&#1080;&#1089;">Привет</a>)");
    EXPECT_EQ(n.AttrOr("v"), "Сервис");
    EXPECT_EQ(n.text, "Привет");
}
