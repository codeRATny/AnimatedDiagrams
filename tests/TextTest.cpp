#include <gtest/gtest.h>

#include "Common/Exceptions.hpp"
#include "Utils/Text.hpp"

using namespace ad;

TEST(TextTest, Base64RoundTrip)
{
    for (const std::string s : {"", "f", "fo", "foo", "foob", "fooba", "foobar"})
    {
        const auto dec = Base64Decode(Base64Encode(s));
        EXPECT_EQ(std::string(dec.begin(), dec.end()), s);
    }
    EXPECT_EQ(Base64Encode(std::string_view("foobar")), "Zm9vYmFy");
    EXPECT_EQ(Base64Encode(std::string_view("fo")), "Zm8=");
    const auto spaced = Base64Decode("Zm9v\nYmFy");
    EXPECT_EQ(std::string(spaced.begin(), spaced.end()), "foobar");
    EXPECT_THROW(Base64Decode("Zm9v*"), ParseError);
}

TEST(TextTest, UrlDecode)
{
    EXPECT_EQ(UrlDecode("%D0%90%20b%2"), "А b%2");
    EXPECT_EQ(UrlDecode("a+b"), "a+b");
}

TEST(TextTest, HtmlHelpers)
{
    EXPECT_EQ(DecodeHtmlEntities("a &amp; b &lt;&gt; &#1040;&#x41; &unknown;"), "a & b <> АA &unknown;");
    EXPECT_EQ(HtmlToText("<div><b>API</b></div><div>gate&nbsp;way</div>"), "API\ngate way");
    EXPECT_EQ(HtmlToText("one<br>two<br/>  three  "), "one\ntwo\nthree");
    EXPECT_EQ(Trim("  x \n"), "x");
}

TEST(TextTest, Slugify)
{
    EXPECT_EQ(Slugify("My Effect 2"), "my-effect-2");
    EXPECT_EQ(Slugify("Эффект"), "item");
    EXPECT_EQ(Slugify("Эффект", "fx"), "fx");
    std::string s;
    AppendUtf8(s, 0x1F600);
    EXPECT_EQ(s.size(), 4U);
}
