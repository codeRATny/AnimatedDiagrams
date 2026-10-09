#include <gtest/gtest.h>

#include <filesystem>
#include <random>

#include "Export/VideoEncoder.hpp"
#include "Utils/File.hpp"

using namespace ad;
using namespace ad::video;
namespace fs = std::filesystem;

namespace
{

constexpr int kW = 64;
constexpr int kH = 48;

std::vector<uint8_t> Frame(int i)
{
    std::vector<uint8_t> rgba(static_cast<size_t>(kW) * kH * 4);
    for (int y = 0; y < kH; ++y)
    {
        for (int x = 0; x < kW; ++x)
        {
            const size_t o = (static_cast<size_t>(y) * kW + x) * 4;
            rgba[o]        = static_cast<uint8_t>((x * 4 + i * 8) & 0xFF);
            rgba[o + 1]    = static_cast<uint8_t>(y * 5);
            rgba[o + 2]    = static_cast<uint8_t>(i * 20);
            rgba[o + 3]    = 255;
        }
    }
    return rgba;
}

fs::path TempFile(const std::string &name)
{
    std::random_device rd;
    const fs::path     dir = fs::temp_directory_path() / ("ad-video-" + std::to_string(rd()));
    fs::create_directories(dir);
    return dir / name;
}

std::string Encode(Container c, const fs::path &path)
{
    VideoEncoder enc;
    const auto   opened = enc.Open(path, {.width = kW, .height = kH, .fps = 10, .container = c, .quality = 1});
    if (!opened.has_value())
    {
        return "open: " + opened.error();
    }
    EXPECT_FALSE(enc.EncoderName().empty());
    for (int i = 0; i < 12; ++i)
    {
        const auto px = Frame(i);
        if (auto r = enc.AddFrame(px); !r.has_value())
        {
            return "frame: " + r.error();
        }
    }
    EXPECT_EQ(enc.FrameCount(), 12);
    if (auto r = enc.Finish(); !r.has_value())
    {
        return "finish: " + r.error();
    }
    EXPECT_TRUE(enc.Finish().has_value()); // idempotent
    return {};
}

} // namespace

TEST(VideoEncoderTest, EncodesWebM)
{
    if (!Available() || EncodersFor(Container::WebM).empty())
    {
        GTEST_SKIP() << "libav without a WebM encoder";
    }
    const fs::path path = TempFile("clip.webm");
    ASSERT_EQ(Encode(Container::WebM, path), "");
    const std::string bytes = ReadFile(path);
    ASSERT_GT(bytes.size(), 100U);
    EXPECT_EQ(bytes.substr(0, 4), std::string("\x1A\x45\xDF\xA3", 4)); // EBML
    fs::remove_all(path.parent_path());
}

TEST(VideoEncoderTest, EncodesMp4WithUtf8Path)
{
    if (!Available())
    {
        GTEST_SKIP() << "built without libav";
    }
    ASSERT_FALSE(EncodersFor(Container::Mp4).empty()); // mpeg4 is always built in
    const fs::path path = TempFile("") / PathFromUtf8("видео.mp4");
    ASSERT_EQ(Encode(Container::Mp4, path), "");
    const std::string bytes = ReadFile(path);
    ASSERT_GT(bytes.size(), 100U);
    EXPECT_EQ(bytes.substr(4, 4), "ftyp");
    fs::remove_all(path.parent_path());
}

TEST(VideoEncoderTest, RejectsBadInput)
{
    VideoEncoder enc;
    EXPECT_FALSE(enc.Open(TempFile("x.mp4"), {.width = 63, .height = 48}).has_value());
    EXPECT_FALSE(enc.AddFrame(Frame(0)).has_value()); // not open
    if (!Available())
    {
        return;
    }
    const fs::path path = TempFile("y.mp4");
    ASSERT_TRUE(enc.Open(path, {.width = kW, .height = kH, .fps = 10, .container = Container::Mp4}).has_value());
    const std::vector<uint8_t> small(16);
    EXPECT_FALSE(enc.AddFrame(small).has_value());
    enc.Abort();
    fs::remove_all(path.parent_path());
}
