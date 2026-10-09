#include "I18n.hpp"

#include <utility>

namespace ad
{

namespace
{

Translator  translator = nullptr;
std::string language   = "en";

} // namespace

void SetTranslator(Translator fn, std::string lang)
{
    translator = fn;
    language   = lang.empty() ? std::string("en") : std::move(lang);
}

const std::string &UiLanguage() { return language; }

std::string Tr(const char *context, const char *text)
{
    if (translator != nullptr)
    {
        if (std::string t = translator(context, text); !t.empty())
        {
            return t;
        }
    }
    return text;
}

} // namespace ad
