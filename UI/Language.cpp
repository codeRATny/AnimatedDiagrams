#include "Language.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>

#include <algorithm>

#include "Utils/I18n.hpp"

namespace ad::ui
{

namespace
{

constexpr auto kLanguageKey = "ui/language";
constexpr auto kPrefix      = "animated-diagrams_";
constexpr auto kBuiltinDir  = ":/i18n";

/// Translation directories, most specific first.
QStringList TranslationDirs()
{
    return {UserTranslationDir(), QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
            QString::fromLatin1(kBuiltinDir)};
}

QString NativeName(const QString &code)
{
    if (code == QStringLiteral("en"))
    {
        return QStringLiteral("English");
    }
    QString name = QLocale(code).nativeLanguageName();
    if (name.isEmpty())
    {
        return code;
    }
    name[0] = name[0].toUpper();
    return name;
}

std::string TranslateCore(const char *context, const char *text) { return QCoreApplication::translate(context, text).toStdString(); }

} // namespace

QString UserTranslationDir() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/translations"); }

std::vector<LanguageInfo> AvailableLanguages()
{
    QStringList                     codes{QStringLiteral("en")};
    static const QRegularExpression kName(QStringLiteral("^animated-diagrams_([a-zA-Z_]+)\\.qm$"));
    for (const QString &dir : TranslationDirs())
    {
        for (const QString &file : QDir(dir).entryList({QStringLiteral("*.qm")}, QDir::Files))
        {
            if (const auto m = kName.match(file); m.hasMatch() && !codes.contains(m.captured(1)))
            {
                codes << m.captured(1);
            }
        }
    }
    codes.sort();
    std::vector<LanguageInfo> out;
    for (const QString &c : codes)
    {
        out.push_back({c, NativeName(c)});
    }
    return out;
}

QString LanguageSetting() { return QSettings().value(kLanguageKey).toString(); }

void SetLanguageSetting(const QString &code) { QSettings().setValue(kLanguageKey, code); }

namespace
{

bool Known(const std::vector<LanguageInfo> &available, const QString &code)
{
    return std::ranges::any_of(available,
                               [&](const LanguageInfo &l)
                               {
                                   return l.code == code;
                               });
}

} // namespace

QString SystemLanguage()
{
    const auto available = AvailableLanguages();
    for (const QString &ui : QLocale::system().uiLanguages()) // "ru-RU", "en-US", ...
    {
        const QString code = QLocale(ui).name().section(QLatin1Char('_'), 0, 0);
        if (Known(available, code))
        {
            return code;
        }
    }
    return QStringLiteral("en");
}

QString ResolveLanguage(const QString &forced)
{
    const auto available = AvailableLanguages();
    for (const QString &candidate : {forced, LanguageSetting()})
    {
        if (!candidate.isEmpty() && Known(available, candidate))
        {
            return candidate;
        }
    }
    return SystemLanguage();
}

QString LanguageName(const QString &code) { return NativeName(code); }

QString InstallTranslations(const QString &forced)
{
    const QString code = ResolveLanguage(forced);
    QLocale::setDefault(QLocale(code));
    if (code != QStringLiteral("en"))
    {
        // the application: first file found wins (user translations override the built-in ones)
        auto *app = new QTranslator(QCoreApplication::instance());
        for (const QString &dir : TranslationDirs())
        {
            if (app->load(QString::fromLatin1(kPrefix) + code, dir))
            {
                QCoreApplication::installTranslator(app);
                break;
            }
        }
        // standard dialogs, buttons and context menus of Qt
        auto       *qt      = new QTranslator(QCoreApplication::instance());
        QStringList qt_dirs = TranslationDirs();
        qt_dirs.prepend(QLibraryInfo::path(QLibraryInfo::TranslationsPath)); // system Qt (Linux packages)
        for (const QString &dir : qt_dirs)
        {
            if (qt->load(QLocale(code), QStringLiteral("qtbase"), QStringLiteral("_"), dir))
            {
                QCoreApplication::installTranslator(qt);
                break;
            }
        }
    }
    SetTranslator(&TranslateCore, code.toStdString());
    return code;
}

} // namespace ad::ui
