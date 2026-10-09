#include <gtest/gtest.h>

#include "Export/HtmlPlayer.hpp"

using namespace ad;

namespace
{

constexpr std::string_view kTemplate = "<html><head><title>{{AD_TITLE}}</title></head><body>"
                                       "<script type=\"application/json\" id=\"ad-player-options\">{{AD_PLAYER_OPTIONS}}</script>"
                                       "<script type=\"application/json\" id=\"ad-document\">{{AD_DOCUMENT_JSON}}</script>"
                                       "<script>engine()</script></body></html>";

} // namespace

TEST(HtmlPlayerTest, EscapeJsonForScript)
{
    // (plain literals: a C++23 compiler may expand a universal character name in a raw string)
    EXPECT_EQ(EscapeJsonForScript(R"({"a":"</script><!-- x"})"), "{\"a\":\"\\u003c/script>\\u003c!-- x\"}");
    EXPECT_EQ(EscapeJsonForScript(R"({"a":1})"), R"({"a":1})");
    EXPECT_EQ(EscapeJsonForScript("{\"ru\":\"Сервис\"}"), "{\"ru\":\"Сервис\"}"); // UTF-8 untouched
}

TEST(HtmlPlayerTest, EscapeHtml) { EXPECT_EQ(EscapeHtml(R"(A & B <x> "q" 's')"), "A &amp; B &lt;x&gt; &quot;q&quot; &#39;s&#39;"); }

TEST(HtmlPlayerTest, OptionsJson)
{
    EXPECT_EQ(PlayerOptionsJson({}), R"({"autoplay":true,"loop":true})");
    EXPECT_EQ(PlayerOptionsJson({.autoplay = false, .loop = false}), R"({"autoplay":false,"loop":false})");
}

TEST(HtmlPlayerTest, FillsPlaceholders)
{
    const auto html = MakePlayerHtml(kTemplate, R"({"meta":{"name":"x</script>"}})", "Flow <1>", {.autoplay = false, .loop = true});
    ASSERT_TRUE(html.has_value()) << html.error();
    EXPECT_NE(html->find("<title>Flow &lt;1&gt;</title>"), std::string::npos);
    EXPECT_NE(html->find(R"(id="ad-player-options">{"autoplay":false,"loop":true}</script>)"), std::string::npos);
    EXPECT_NE(html->find("id=\"ad-document\">{\"meta\":{\"name\":\"x\\u003c/script>\"}}</script>"), std::string::npos);
    EXPECT_EQ(html->find("{{AD_"), std::string::npos);
    // the document cannot close its element: the only </script> tags are the template's
    size_t count = 0;
    for (size_t p = html->find("</script>"); p != std::string::npos; p = html->find("</script>", p + 1))
    {
        ++count;
    }
    EXPECT_EQ(count, 3U);
}

TEST(HtmlPlayerTest, InjectedTextIsNotReplacedAgain)
{
    // placeholders inside the title / document stay literal text
    const auto html = MakePlayerHtml(kTemplate, R"({"a":"{{AD_TITLE}}"})", "{{AD_DOCUMENT_JSON}}");
    ASSERT_TRUE(html.has_value());
    EXPECT_NE(html->find("<title>{{AD_DOCUMENT_JSON}}</title>"), std::string::npos);
    EXPECT_NE(html->find(R"(id="ad-document">{"a":"{{AD_TITLE}}"}</script>)"), std::string::npos);
}

TEST(HtmlPlayerTest, EmptyTitleGetsDefault)
{
    const auto html = MakePlayerHtml(kTemplate, "{}", "");
    ASSERT_TRUE(html.has_value());
    EXPECT_NE(html->find("<title>Animated diagram</title>"), std::string::npos);
}

TEST(HtmlPlayerTest, RejectsForeignTemplate)
{
    const auto html = MakePlayerHtml("<html><title>{{AD_TITLE}}</title></html>", "{}", "x");
    ASSERT_FALSE(html.has_value());
    EXPECT_NE(html.error().find("{{AD_PLAYER_OPTIONS}}"), std::string::npos);
}
