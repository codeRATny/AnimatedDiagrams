#ifndef _UI_CRASH_REPORTS_HPP_
#define _UI_CRASH_REPORTS_HPP_

#include <QString>
#include <QStringList>

class QWidget;

/// @file CrashReports.hpp
/// @brief Crash reporting in the application: installs the crash handler, records Qt
///        warnings as breadcrumbs and tells the user about reports left by a crash.

namespace ad::ui
{

/// Directory of the crash reports (application data / crashes).
[[nodiscard]] QString CrashReportDir();

/// Install the crash handler and the Qt message breadcrumbs (after the QApplication
/// names are set: the directory depends on them).
void InstallCrashReporting();

/// Reports written after the last MarkCrashReportsSeen() call, newest first.
[[nodiscard]] QStringList NewCrashReports();
void                      MarkCrashReportsSeen();

/// After a crash: tell the user where the report is (once per report).
void NotifyAboutCrashReports(QWidget *parent);

/// Open the reports directory in the file manager.
void OpenCrashReportDir();

} // namespace ad::ui

#endif // _UI_CRASH_REPORTS_HPP_
