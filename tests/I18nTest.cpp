#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "Plugins/Plugin.hpp"
#include "Utils/File.hpp"
#include "Utils/I18n.hpp"

using namespace ad;

namespace
{

std::string FakeTranslator(const char *context, const char *text)
{
    if (std::strcmp(context, "test") == 0 && std::strcmp(text, "Hello") == 0)
    {
        return "Привет";
    }
    return {}; // no translation
}

} // namespace

TEST(I18nTest, TranslatorAndFallback)
{
    EXPECT_EQ(Tr("test", "Hello"), "Hello"); // no translator: the English source
    EXPECT_EQ(UiLanguage(), "en");
    SetTranslator(&FakeTranslator, "ru");
    EXPECT_EQ(Tr("test", "Hello"), "Привет");
    EXPECT_EQ(Tr("test", "Bye"), "Bye");
    EXPECT_EQ(UiLanguage(), "ru");
    SetTranslator(nullptr, "");
    EXPECT_EQ(Tr("test", "Hello"), "Hello");
    EXPECT_EQ(UiLanguage(), "en");
}

TEST(I18nTest, PluginTranslations)
{
    auto plugin = ParsePlugin(R"({"format": "animated-diagrams-plugin", "id": "x.i18n", "name": "Kit", "description": "Demo",
        "elements": [{"id": "broker", "label": "Broker", "category": "Messaging"}],
        "animations": [{"id": "a", "label": "Flow", "roles": [{"id": "p", "label": "Producer"}],
                        "steps": [{"id": "s", "type": "message", "from": "p", "to": "p", "label": "event"}]}],
        "translations": {"ru": {"Kit": "Набор", "Broker": "Брокер", "Messaging": "Сообщения", "Producer": "Издатель",
                                "event": "событие"}}})");
    ASSERT_TRUE(plugin.has_value()) << plugin.error();
    ASSERT_EQ(plugin->translations.at("ru").size(), 5U);

    Plugin german = *plugin;
    LocalizePlugin(german, "de"); // no German texts: unchanged
    EXPECT_EQ(german, *plugin);

    // the manifest keeps the dictionary
    const auto again = ParsePlugin(SerializePlugin(*plugin));
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(again->translations, plugin->translations);

    LocalizePlugin(*plugin, "ru");
    EXPECT_EQ(plugin->info.name, "Набор");
    EXPECT_EQ(plugin->info.description, "Demo"); // untranslated text is kept
    EXPECT_EQ(plugin->library.elements[0].label, "Брокер");
    EXPECT_EQ(plugin->library.elements[0].category, "Сообщения");
    EXPECT_EQ(plugin->library.animations[0].roles[0].label, "Издатель");
    EXPECT_EQ(plugin->library.animations[0].steps[0].label, "событие");
}

TEST(I18nTest, BundledPluginsAreTranslatedToRussian)
{
    for (const char *name : {"cloud-kit.json", "data-platform.json", "security-kit.json"})
    {
        SCOPED_TRACE(name);
        auto plugin = ParsePlugin(ReadFile(std::string(AD_PLUGINS_DIR) + "/" + name));
        ASSERT_TRUE(plugin.has_value()) << plugin.error();
        ASSERT_TRUE(plugin->translations.contains("ru"));
        const std::string english = plugin->info.description;
        LocalizePlugin(*plugin, "ru");
        EXPECT_NE(plugin->info.description, english);
    }
}
