#include <gtest/gtest.h>

#include <csignal>
#include <exception>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>

#include "Utils/CrashHandler.hpp"
#include "Utils/File.hpp"

using namespace ad;
namespace fs = std::filesystem;

namespace
{

fs::path TempDir() { return fs::temp_directory_path() / ("ad-crash-test-" + std::to_string(std::random_device{}())); }

[[noreturn]] void CrashWith(const fs::path &dir, int kind)
{
    crash::Install(dir, "test 1.0");
    crash::Breadcrumb("MCP tools/call add_node");
    crash::Breadcrumb("second\nline");
    if (kind == 0)
    {
        std::raise(SIGSEGV);
    }
    try
    {
        throw std::runtime_error("boom");
    }
    catch (...)
    {
        std::terminate(); // as for an exception escaping a noexcept function / a thread
    }
}

} // namespace

TEST(CrashHandler, BreadcrumbsKeepTheLastLines)
{
    for (int i = 0; i < crash::kBreadcrumbs + 10; ++i)
    {
        crash::Breadcrumb("event " + std::to_string(i));
    }
    crash::Breadcrumb(std::string(1000, 'x') + "\nnext");
    const auto lines = crash::Breadcrumbs();
    ASSERT_EQ(lines.size(), static_cast<size_t>(crash::kBreadcrumbs));
    EXPECT_NE(lines.front().find("event 11"), std::string::npos);
    EXPECT_LT(lines.back().size(), 300U);
    EXPECT_EQ(lines.back().find('\n'), std::string::npos);
}

TEST(CrashHandler, ReportsAreListedNewestFirst)
{
    const fs::path dir = TempDir();
    fs::create_directories(dir);
    WriteFile(dir / "crash-000000000100-1.txt", "a");
    WriteFile(dir / "crash-000000000200-1.txt", "b");
    WriteFile(dir / "crash-000000000200-1.dmp", "c");
    WriteFile(dir / "other.txt", "d");
    const auto reports = crash::Reports(dir);
    ASSERT_EQ(reports.size(), 2U);
    EXPECT_EQ(reports[0].filename(), "crash-000000000200-1.txt");
    EXPECT_TRUE(crash::Reports(dir / "missing").empty());
    fs::remove_all(dir);
}

#if GTEST_HAS_DEATH_TEST && !defined(_WIN32)

TEST(CrashHandler, WritesReportOnFatalSignalAndUncaughtException)
{
    if (!crash::HandlesFatalErrors())
    {
        GTEST_SKIP() << "sanitizer build: ASan reports fatal errors itself";
    }
    for (int kind = 0; kind < 2; ++kind)
    {
        // fixed name: the "threadsafe" death test style re-runs the binary for the child
        const fs::path dir = fs::temp_directory_path() / ("ad-crash-test-death-" + std::to_string(kind));
        fs::remove_all(dir);
        EXPECT_DEATH(CrashWith(dir, kind), "");
        const auto reports = crash::Reports(dir);
        ASSERT_EQ(reports.size(), 1U);
        const std::string text = ReadFile(reports.front());
        EXPECT_NE(text.find("Application: test 1.0"), std::string::npos) << text;
        EXPECT_NE(text.find(kind == 0 ? "SIGSEGV" : "uncaught exception: boom"), std::string::npos) << text;
        EXPECT_NE(text.find("Stack trace"), std::string::npos);
        EXPECT_NE(text.find("MCP tools/call add_node"), std::string::npos);
        EXPECT_NE(text.find("second line"), std::string::npos);
        fs::remove_all(dir);
    }
}

#endif
