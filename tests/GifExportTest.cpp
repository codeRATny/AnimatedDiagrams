#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <random>

#include "Export/VideoEncoder.hpp"
#include "Utils/File.hpp"

#ifdef AD_HAVE_LIBAV
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#endif

using namespace ad;
using namespace ad::video;
namespace fs = std::filesystem;

namespace
{

constexpr int kW = 37; // GIF sizes need not be even
constexpr int kH = 23;

using Rgb = std::array<uint8_t, 3>;

constexpr std::array<Rgb, 4> kColors{Rgb{220, 40, 30}, Rgb{30, 60, 200}, Rgb{10, 17, 31}, Rgb{250, 250, 250}};

/// Solid background with a moving 8x8 square.
std::vector<uint8_t> Frame(int i)
{
    std::vector<uint8_t> rgba(static_cast<size_t>(kW) * kH * 4);
    for (int y = 0; y < kH; ++y)
    {
        for (int x = 0; x < kW; ++x)
        {
            const bool   square = x >= i * 3 && x < i * 3 + 8 && y >= 4 && y < 12;
            const Rgb   &c      = square ? kColors.at(static_cast<size_t>(i) % 2) : kColors[2];
            const size_t o      = (static_cast<size_t>(y) * kW + static_cast<size_t>(x)) * 4;
            rgba[o]             = c[0];
            rgba[o + 1]         = c[1];
            rgba[o + 2]         = c[2];
            rgba[o + 3]         = 255;
        }
    }
    return rgba;
}

fs::path TempFile(const std::string &name)
{
    std::random_device rd;
    const fs::path     dir = fs::temp_directory_path() / ("ad-gif-" + std::to_string(rd()));
    fs::create_directories(dir);
    return dir / name;
}

/// Encode `frames` frames in both passes; error text or empty.
std::string EncodeGif(const fs::path &path, int frames, double fps, bool loop)
{
    VideoEncoder enc;
    if (auto r = enc.Open(path, {.width = kW, .height = kH, .fps = fps, .container = Container::Gif, .loop = loop}); !r.has_value())
    {
        return "open: " + r.error();
    }
    EXPECT_EQ(enc.EncoderName(), "gif");
    for (int pass = 0; pass < PassCount(Container::Gif); ++pass)
    {
        if (pass > 0)
        {
            if (auto r = enc.NextPass(); !r.has_value())
            {
                return "next pass: " + r.error();
            }
        }
        EXPECT_EQ(enc.Pass(), pass);
        for (int i = 0; i < frames; ++i)
        {
            if (auto r = enc.AddFrame(Frame(i)); !r.has_value())
            {
                return "frame: " + r.error();
            }
        }
        EXPECT_EQ(enc.FrameCount(), frames);
    }
    if (auto r = enc.Finish(); !r.has_value())
    {
        return "finish: " + r.error();
    }
    EXPECT_TRUE(enc.Finish().has_value()); // idempotent
    return {};
}

#ifdef AD_HAVE_LIBAV

struct Demuxed
{
    std::vector<int64_t> pts; // 1/100 s
    std::vector<Rgb>     first_pixel;
    std::vector<Rgb>     square_pixel; // pixel (4, 6): inside the square of frames 0 and 1
    int                  width  = 0;
    int                  height = 0;
};

/// Demux and decode the file with libavformat / libavcodec.
Demuxed Demux(const fs::path &path)
{
    Demuxed          out;
    AVFormatContext *fmt  = nullptr;
    const auto       file = PathToUtf8(path);
    if (avformat_open_input(&fmt, file.c_str(), nullptr, nullptr) < 0)
    {
        ADD_FAILURE() << "libavformat cannot open " << file;
        return out;
    }
    avformat_find_stream_info(fmt, nullptr);
    const AVStream *st      = fmt->streams[0];
    const AVCodec  *decoder = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext *dec     = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(dec, st->codecpar);
    EXPECT_GE(avcodec_open2(dec, decoder, nullptr), 0);
    AVPacket *pkt   = av_packet_alloc();
    AVFrame  *frame = av_frame_alloc();
    out.width       = st->codecpar->width;
    out.height      = st->codecpar->height;

    auto take = [&]()
    {
        while (avcodec_receive_frame(dec, frame) >= 0)
        {
            std::vector<uint8_t> rgba(static_cast<size_t>(frame->width) * static_cast<size_t>(frame->height) * 4);
            SwsContext          *sws = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), frame->width,
                                                      frame->height, AV_PIX_FMT_RGBA, SWS_POINT, nullptr, nullptr, nullptr);
            std::array<uint8_t *, 1> dst{rgba.data()};
            std::array<int, 1>       stride{frame->width * 4};
            sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst.data(), stride.data());
            sws_freeContext(sws);
            auto px = [&](int x, int y)
            {
                const size_t o = (static_cast<size_t>(y) * static_cast<size_t>(frame->width) + static_cast<size_t>(x)) * 4;
                return Rgb{rgba[o], rgba[o + 1], rgba[o + 2]};
            };
            out.first_pixel.push_back(px(0, 0));
            out.square_pixel.push_back(px(4, 6));
        }
    };
    while (av_read_frame(fmt, pkt) >= 0)
    {
        out.pts.push_back(av_rescale_q(pkt->pts, st->time_base, {1, 100}));
        avcodec_send_packet(dec, pkt);
        av_packet_unref(pkt);
        take();
    }
    avcodec_send_packet(dec, nullptr);
    take();
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return out;
}

#endif

} // namespace

TEST(GifExportTest, EncodesLoopingGifWithOnePalette)
{
    if (!Available() || EncodersFor(Container::Gif).empty())
    {
        GTEST_SKIP() << "libav without the GIF encoder or palette filters";
    }
    const fs::path path = TempFile("anim.gif");
    ASSERT_EQ(EncodeGif(path, 6, 10, true), "");
    const std::string bytes = ReadFile(path);
    ASSERT_GT(bytes.size(), 13U);
    EXPECT_EQ(bytes.substr(0, 6), "GIF89a");
    EXPECT_EQ(static_cast<uint8_t>(bytes[6]) | (static_cast<uint8_t>(bytes[7]) << 8U), kW); // logical screen
    EXPECT_EQ(static_cast<uint8_t>(bytes[8]) | (static_cast<uint8_t>(bytes[9]) << 8U), kH);
    EXPECT_NE(bytes.find("NETSCAPE2.0"), std::string::npos); // loop extension
    EXPECT_EQ(static_cast<uint8_t>(bytes.back()), 0x3B);     // trailer

#ifdef AD_HAVE_LIBAV
    const Demuxed d = Demux(path);
    EXPECT_EQ(d.width, kW);
    EXPECT_EQ(d.height, kH);
    EXPECT_EQ(d.pts, (std::vector<int64_t>{0, 10, 20, 30, 40, 50})); // 10 fps
    ASSERT_EQ(d.first_pixel.size(), 6U);
    for (size_t i = 0; i < d.first_pixel.size(); ++i)
    {
        EXPECT_EQ(d.first_pixel[i], kColors[2]) << "frame " << i; // few colors: the palette is exact
    }
    EXPECT_EQ(d.square_pixel[0], kColors[0]);
    EXPECT_EQ(d.square_pixel[1], kColors[1]);
#endif
    fs::remove_all(path.parent_path());
}

TEST(GifExportTest, NoLoopExtensionAndMinimumDelay)
{
    if (!Available() || EncodersFor(Container::Gif).empty())
    {
        GTEST_SKIP() << "libav without the GIF encoder or palette filters";
    }
    const fs::path path = TempFile("once.gif");
    ASSERT_EQ(EncodeGif(path, 5, 60, false), "");
    const std::string bytes = ReadFile(path);
    EXPECT_EQ(bytes.substr(0, 6), "GIF89a");
    EXPECT_EQ(bytes.find("NETSCAPE2.0"), std::string::npos);
#ifdef AD_HAVE_LIBAV
    const Demuxed d = Demux(path);
    ASSERT_EQ(d.pts.size(), 5U);
    for (size_t i = 1; i < d.pts.size(); ++i)
    {
        EXPECT_GE(d.pts[i] - d.pts[i - 1], 2); // browsers stretch shorter delays
    }
#endif
    fs::remove_all(path.parent_path());
}

TEST(GifExportTest, RequiresBothPasses)
{
    VideoEncoder   enc;
    const fs::path path = TempFile("passes.gif");
    const auto     open = enc.Open(path, {.width = kW, .height = kH, .container = Container::Gif});
    if (!Available() || EncodersFor(Container::Gif).empty())
    {
        EXPECT_FALSE(open.has_value()); // reported like WebM / MP4 without libav
        fs::remove_all(path.parent_path());
        return;
    }
    ASSERT_TRUE(open.has_value()) << open.error();
    EXPECT_FALSE(enc.NextPass().has_value()); // no frames for the palette
    enc.Abort();

    ASSERT_TRUE(enc.Open(path, {.width = kW, .height = kH, .container = Container::Gif}).has_value());
    ASSERT_TRUE(enc.AddFrame(Frame(0)).has_value());
    EXPECT_FALSE(enc.Finish().has_value()); // the encoding pass is missing
    enc.Abort();

    EXPECT_FALSE(enc.Open(path, {.width = 0, .height = kH, .container = Container::Gif}).has_value());
    EXPECT_EQ(PassCount(Container::Gif), 2);
    EXPECT_EQ(PassCount(Container::Mp4), 1);
    fs::remove_all(path.parent_path());
}
