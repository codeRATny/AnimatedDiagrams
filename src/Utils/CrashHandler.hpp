#ifndef _UTILS_CRASH_HANDLER_HPP_
#define _UTILS_CRASH_HANDLER_HPP_

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

/// @file CrashHandler.hpp
/// @brief Crash reports: on a fatal signal (Linux) or an unhandled exception (Windows)
///        a text report is written to the reports directory: reason, stack trace,
///        the last breadcrumbs (MCP calls, warnings, user actions); on Windows also a
///        minidump. The handler only uses preallocated memory and async-signal-safe calls.

namespace ad::crash
{

/// Install the handlers (once per process); reports go to `dir` (created when missing).
/// `app` (name and version) is written into every report.
void Install(const std::filesystem::path &dir, std::string_view app);

/// Whether Install() was called.
[[nodiscard]] bool Installed();

/// False in AddressSanitizer builds: ASan keeps its own, more precise, fatal error reports.
[[nodiscard]] bool HandlesFatalErrors();

/// Remember a line for the next report (the last kBreadcrumbs lines are kept; long lines
/// are truncated). Thread-safe, cheap, works without Install().
void                 Breadcrumb(std::string_view line);
inline constexpr int kBreadcrumbs = 64;

/// Breadcrumbs currently kept, oldest first (for tests and diagnostics).
[[nodiscard]] std::vector<std::string> Breadcrumbs();

/// Reports in `dir` (crash-*.txt), newest first.
[[nodiscard]] std::vector<std::filesystem::path> Reports(const std::filesystem::path &dir);

} // namespace ad::crash

#endif // _UTILS_CRASH_HANDLER_HPP_
