#ifndef _UI_LANGUAGE_HPP_
#define _UI_LANGUAGE_HPP_

#include <QString>
#include <QStringList>

#include <vector>

/// @file Language.hpp
/// @brief Interface language: English source strings, translations from Qt .qm files
///        (built in, or dropped into <app data>/translations), Qt's own dialogs and the
///        core library (labels of built-in definitions). Applied at startup, so a change
///        takes effect after a restart.

namespace ad::ui
{

struct LanguageInfo
{
    QString code;        // "en", "ru", ...
    QString native_name; // "English", "Русский", ... (in the language itself)
};

/// English plus every language with a translation file, sorted by code.
[[nodiscard]] std::vector<LanguageInfo> AvailableLanguages();

/// Stored choice; empty -- follow the system language.
[[nodiscard]] QString LanguageSetting();
void                  SetLanguageSetting(const QString &code);

/// System UI language limited to the available ones (English when nothing matches).
[[nodiscard]] QString SystemLanguage();

/// Name of a language in the language itself ("English", "Русский").
[[nodiscard]] QString LanguageName(const QString &code);

/// `forced` (e.g. --lang) or the stored choice or the system language, limited to the
/// available languages (English when nothing matches).
[[nodiscard]] QString ResolveLanguage(const QString &forced = {});

/// Install the translators of the application, of Qt and of the core library.
/// Call right after the QApplication is created (organization / application names set),
/// before any window or library definition is created. Returns the language in use.
QString InstallTranslations(const QString &forced = {});

/// Directory for additional translations (animated-diagrams_<code>.qm).
[[nodiscard]] QString UserTranslationDir();

} // namespace ad::ui

#endif // _UI_LANGUAGE_HPP_
