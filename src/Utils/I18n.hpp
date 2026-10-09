#ifndef _UTILS_I18N_HPP_
#define _UTILS_I18N_HPP_

#include <string>

/// @file I18n.hpp
/// @brief Translation of user-visible core strings (catalog labels, built-in library,
///        the sample document). Source strings are English. The application installs a
///        translator (Qt translations) at startup, before any lookup; built-in tables are
///        created on first use, so a language change applies after a restart.
///
/// Strings are marked for lupdate with Tr("context", "text") (alias of translate) or
/// AD_TR_NOOP("context", "text") (alias of QT_TRANSLATE_NOOP).

#define AD_TR_NOOP(context, text) text

namespace ad
{

/// Returns the translation of `text` or nullptr / an empty string when there is none.
using Translator = std::string (*)(const char *context, const char *text);

/// Install the translator and the UI language code ("en", "ru", ...).
void SetTranslator(Translator fn, std::string language);

/// Current UI language code; "en" when no translator is installed.
[[nodiscard]] const std::string &UiLanguage();

/// Translation of `text` in `context`, or `text` itself.
[[nodiscard]] std::string Tr(const char *context, const char *text);

} // namespace ad

#endif // _UTILS_I18N_HPP_
