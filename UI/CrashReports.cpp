#include "CrashReports.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

#include "QtRender.hpp"
#include "Utils/CrashHandler.hpp"
#include "Utils/File.hpp"

namespace ad::ui
{

namespace
{

constexpr auto kSeenKey = "crash/lastSeenReport";

QtMessageHandler previous_handler = nullptr;

void OnQtMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type != QtDebugMsg && type != QtInfoMsg)
    {
        static constexpr const char *kTypes[] = {"debug", "warning", "critical", "fatal", "info"};
        const auto                   index    = static_cast<size_t>(type);
        crash::Breadcrumb(std::string("Qt ") + (index < std::size(kTypes) ? kTypes[index] : "message") + ": " + message.toStdString());
    }
    if (previous_handler != nullptr)
    {
        previous_handler(type, context, message);
    }
}

} // namespace

QString CrashReportDir() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/crashes"); }

void InstallCrashReporting()
{
    const QString app = QStringLiteral("%1 %2 (Qt %3, %4)")
                            .arg(QApplication::applicationDisplayName(), QApplication::applicationVersion(),
                                 QString::fromLatin1(qVersion()), QSysInfo::prettyProductName());
    crash::Install(PathFromUtf8(Us(CrashReportDir())), Us(app));
    previous_handler = qInstallMessageHandler(OnQtMessage);
    crash::Breadcrumb("start: " + Us(app));
}

QStringList NewCrashReports()
{
    const QString seen = QSettings().value(kSeenKey).toString();
    QStringList   out;
    for (const auto &p : crash::Reports(PathFromUtf8(Us(CrashReportDir()))))
    {
        const QString name = Qs(PathToUtf8(p.filename()));
        if (!seen.isEmpty() && name <= seen)
        {
            break; // newest first: the rest was already shown
        }
        out << Qs(PathToUtf8(p));
    }
    return out;
}

void MarkCrashReportsSeen()
{
    const auto reports = crash::Reports(PathFromUtf8(Us(CrashReportDir())));
    if (!reports.empty())
    {
        QSettings().setValue(kSeenKey, Qs(PathToUtf8(reports.front().filename())));
    }
}

void OpenCrashReportDir()
{
    QDir().mkpath(CrashReportDir());
    QDesktopServices::openUrl(QUrl::fromLocalFile(CrashReportDir()));
}

void NotifyAboutCrashReports(QWidget *parent)
{
    const QStringList reports = NewCrashReports();
    if (reports.isEmpty())
    {
        return;
    }
    MarkCrashReportsSeen();
    const QString path = QDir::toNativeSeparators(reports.front());
    QMessageBox   box(QMessageBox::Warning, QObject::tr("Unexpected Shutdown"),
                      QObject::tr("The previous session ended unexpectedly. Open tabs have been restored from autosave.\n\n"
                                    "A crash report was saved to:\n%1\n\nPlease attach it to your bug report.")
                          .arg(path),
                      QMessageBox::Close, parent);
    QPushButton  *open = box.addButton(QObject::tr("Open folder"), QMessageBox::ActionRole);
    QPushButton  *copy = box.addButton(QObject::tr("Copy path"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == open)
    {
        OpenCrashReportDir();
    }
    else if (box.clickedButton() == copy)
    {
        QApplication::clipboard()->setText(path);
    }
}

} // namespace ad::ui
