#include <gtest/gtest.h>

#include <filesystem>
#include <random>

#include "Common/Exceptions.hpp"
#include "Utils/File.hpp"

using namespace ad;
namespace fs = std::filesystem;

TEST(FileTest, WriteReadRoundTripWithUtf8Path)
{
    const fs::path    dir  = fs::temp_directory_path() / ("ad-file-test-" + std::to_string(std::random_device{}()));
    const fs::path    file = dir / PathFromUtf8("вложенная/диаграмма.json");
    const std::string data("binary\0data\xff", 12);
    WriteFile(file, data);
    EXPECT_EQ(ReadFile(file), data);
    EXPECT_TRUE(PathToUtf8(file).ends_with("диаграмма.json"));
    WriteFile(file, "second");
    EXPECT_EQ(ReadFile(file), "second");
    EXPECT_FALSE(fs::exists(fs::path(file) += ".tmp")); // no narrow conversion: fails on Windows ANSI code pages
    std::error_code ec;
    fs::remove_all(dir, ec);
    EXPECT_THROW(ReadFile(dir / "missing"), IoError);
}
