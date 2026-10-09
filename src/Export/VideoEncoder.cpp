#include "VideoEncoder.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "Utils/File.hpp"

#ifdef AD_HAVE_LIBAV
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#endif

namespace ad::video
{

#ifdef AD_HAVE_LIBAV

namespace
{

/// Candidate encoders in preference order.
std::vector<const char *> Candidates(Container c)
{
    if (c == Container::WebM)
    {
        return {"libvpx-vp9", "libvpx", "libaom-av1", "libsvtav1"};
    }
    return {"libx264", "libopenh264", "h264_mf", "mpeg4"};
}

const char *MuxerName(Container c) { return c == Container::WebM ? "webm" : "mp4"; }

std::string AvError(int code)
{
    std::array<char, 256> buf{};
    av_strerror(code, buf.data(), buf.size());
    return {buf.data()};
}

std::vector<AVPixelFormat> PixelFormats(const AVCodec *codec)
{
    std::vector<AVPixelFormat> out;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const void *cfg = nullptr;
    int         n   = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &cfg, &n) >= 0 && cfg != nullptr)
    {
        const auto *fmts = static_cast<const AVPixelFormat *>(cfg);
        out.assign(fmts, fmts + n);
    }
#else
    for (const AVPixelFormat *p = codec->pix_fmts; p != nullptr && *p != AV_PIX_FMT_NONE; ++p)
    {
        out.push_back(*p);
    }
#endif
    return out;
}

AVPixelFormat PickPixelFormat(const AVCodec *codec)
{
    const auto fmts = PixelFormats(codec);
    if (fmts.empty() || std::ranges::find(fmts, AV_PIX_FMT_YUV420P) != fmts.end())
    {
        return AV_PIX_FMT_YUV420P;
    }
    return fmts.front();
}

/// Encoder-specific rate control for the quality level 0..4.
void Configure(AVCodecContext *c, const std::string &name, int quality, AVDictionary **opts)
{
    static constexpr std::array kBitsPerPixel{0.04, 0.07, 0.1, 0.15, 0.22};
    const auto                  q       = static_cast<size_t>(std::clamp(quality, 0, 4));
    const double                fps     = av_q2d(c->framerate);
    const auto                  bitrate = static_cast<int64_t>(c->width * c->height * fps * kBitsPerPixel.at(q));
    auto                        set     = [opts](const char *key, const char *value)
    {
        av_dict_set(opts, key, value, 0);
    };
    auto set_int = [opts](const char *key, int value)
    {
        av_dict_set_int(opts, key, value, 0);
    };

    if (name == "libx264")
    {
        static constexpr std::array kCrf{30, 26, 21, 18, 15};
        set("preset", "medium");
        set_int("crf", kCrf.at(q));
    }
    else if (name == "libvpx-vp9" || name == "libaom-av1")
    {
        static constexpr std::array kCrf{44, 38, 32, 27, 22};
        c->bit_rate = 0; // constant quality
        set_int("crf", kCrf.at(q));
        set("row-mt", "1");
        if (name == "libvpx-vp9")
        {
            set("deadline", "good");
            set("cpu-used", "4");
        }
        else
        {
            set("cpu-used", "6");
        }
    }
    else if (name == "libvpx")
    {
        static constexpr std::array kCrf{40, 30, 20, 12, 6};
        c->bit_rate = bitrate * 2; // VP8 uses the bitrate as the cap of the constrained quality mode
        set_int("crf", kCrf.at(q));
        set("deadline", "good");
        set("cpu-used", "4");
    }
    else if (name == "libsvtav1")
    {
        static constexpr std::array kCrf{50, 42, 35, 28, 22};
        set_int("crf", kCrf.at(q));
        set("preset", "8");
    }
    else if (name == "mpeg4")
    {
        static constexpr std::array kQscale{10, 7, 5, 3, 2};
        c->flags |= AV_CODEC_FLAG_QSCALE;
        c->global_quality = FF_QP2LAMBDA * kQscale.at(q);
    }
    else // libopenh264, h264_mf and others: plain bitrate
    {
        c->bit_rate = bitrate;
    }
}

} // namespace

struct VideoEncoder::Impl
{
    AVFormatContext *fmt    = nullptr;
    AVCodecContext  *codec  = nullptr;
    AVStream        *stream = nullptr;
    AVFrame         *frame  = nullptr;
    AVPacket        *packet = nullptr;
    SwsContext      *sws    = nullptr;
    std::string      encoder;
    int              width    = 0;
    int              height   = 0;
    int64_t          next_pts = 0;
    bool             header   = false;
    bool             finished = false;

    ~Impl() { Release(); }
    Impl()                        = default;
    Impl(const Impl &)            = delete;
    Impl &operator=(const Impl &) = delete;

    void Reset()
    {
        Release();
        encoder.clear();
        width    = 0;
        height   = 0;
        next_pts = 0;
        header   = false;
        finished = false;
    }

    void Release()
    {
        sws_freeContext(sws);
        sws = nullptr;
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        if (fmt != nullptr)
        {
            if (fmt->pb != nullptr && (fmt->oformat->flags & AVFMT_NOFILE) == 0)
            {
                avio_closep(&fmt->pb);
            }
            avformat_free_context(fmt);
            fmt = nullptr;
        }
        stream = nullptr;
    }

    /// Send a frame (nullptr = flush) and write every packet the encoder returns.
    std::expected<void, std::string> Encode(const AVFrame *f)
    {
        int rc = avcodec_send_frame(codec, f);
        if (rc < 0)
        {
            return std::unexpected("encoding failed: " + AvError(rc));
        }
        while (true)
        {
            rc = avcodec_receive_packet(codec, packet);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            {
                return {};
            }
            if (rc < 0)
            {
                return std::unexpected("encoding failed: " + AvError(rc));
            }
            av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
            packet->stream_index = stream->index;
            rc                   = av_interleaved_write_frame(fmt, packet);
            av_packet_unref(packet);
            if (rc < 0)
            {
                return std::unexpected("cannot write the video: " + AvError(rc));
            }
        }
    }
};

bool Available() { return true; }

std::string LibraryVersions()
{
    auto ver = [](const char *name, unsigned v)
    {
        return std::string(name) + " " + std::to_string(v >> 16U) + "." + std::to_string((v >> 8U) & 0xFFU) + "." +
               std::to_string(v & 0xFFU);
    };
    return ver("libavcodec", avcodec_version()) + ", " + ver("libavformat", avformat_version()) + ", " +
           ver("libswscale", swscale_version());
}

std::vector<std::string> EncodersFor(Container c)
{
    std::vector<std::string> out;
    const AVOutputFormat    *muxer = av_guess_format(MuxerName(c), nullptr, nullptr);
    if (muxer == nullptr)
    {
        return out;
    }
    for (const char *name : Candidates(c))
    {
        const AVCodec *codec = avcodec_find_encoder_by_name(name);
        if (codec != nullptr && avformat_query_codec(muxer, codec->id, FF_COMPLIANCE_NORMAL) == 1)
        {
            out.emplace_back(name);
        }
    }
    return out;
}

VideoEncoder::VideoEncoder() : _impl(std::make_unique<Impl>()) {}

VideoEncoder::~VideoEncoder() = default;

std::expected<void, std::string> VideoEncoder::Open(const std::filesystem::path &path, const VideoOptions &opt)
{
    Impl &d = *_impl;
    d.Reset();
    if (opt.width <= 0 || opt.height <= 0 || opt.width % 2 != 0 || opt.height % 2 != 0)
    {
        return std::unexpected("video size must be positive and even");
    }
    if (opt.fps <= 0 || opt.fps > 240)
    {
        return std::unexpected("invalid frame rate");
    }
    av_log_set_level(AV_LOG_ERROR);

    const std::string file = PathToUtf8(path);
    int               rc   = avformat_alloc_output_context2(&d.fmt, nullptr, MuxerName(opt.container), file.c_str());
    if (rc < 0 || d.fmt == nullptr)
    {
        return std::unexpected("cannot create the container: " + AvError(rc));
    }

    // first encoder that opens wins (e.g. a stubbed openh264 falls through to mpeg4)
    std::string tried;
    for (const std::string &name : EncodersFor(opt.container))
    {
        const AVCodec  *codec = avcodec_find_encoder_by_name(name.c_str());
        AVCodecContext *c     = avcodec_alloc_context3(codec);
        c->width              = opt.width;
        c->height             = opt.height;
        c->framerate          = av_d2q(opt.fps, 100000);
        c->time_base          = av_inv_q(c->framerate);
        c->pix_fmt            = PickPixelFormat(codec);
        c->gop_size           = std::max(1, static_cast<int>(std::lround(opt.fps * 2)));
        if ((d.fmt->oformat->flags & AVFMT_GLOBALHEADER) != 0)
        {
            c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        AVDictionary *codec_opts = nullptr;
        Configure(c, name, opt.quality, &codec_opts);
        rc = avcodec_open2(c, codec, &codec_opts);
        av_dict_free(&codec_opts);
        if (rc >= 0)
        {
            d.codec   = c;
            d.encoder = name;
            break;
        }
        tried += (tried.empty() ? "" : ", ") + name + " (" + AvError(rc) + ")";
        avcodec_free_context(&c);
    }
    if (d.codec == nullptr)
    {
        d.Release();
        return std::unexpected(std::string("no usable ") +
                               (opt.container == Container::WebM ? "WebM (VP9/VP8/AV1)" : "MP4 (H.264/MPEG-4)") + " encoder in libav" +
                               (tried.empty() ? std::string() : ": " + tried));
    }

    d.stream = avformat_new_stream(d.fmt, nullptr);
    if (d.stream == nullptr)
    {
        d.Release();
        return std::unexpected("cannot create the video stream");
    }
    d.stream->time_base      = d.codec->time_base;
    d.stream->avg_frame_rate = d.codec->framerate;
    rc                       = avcodec_parameters_from_context(d.stream->codecpar, d.codec);
    if (rc < 0)
    {
        d.Release();
        return std::unexpected("cannot set stream parameters: " + AvError(rc));
    }

    if ((d.fmt->oformat->flags & AVFMT_NOFILE) == 0)
    {
        rc = avio_open(&d.fmt->pb, file.c_str(), AVIO_FLAG_WRITE);
        if (rc < 0)
        {
            d.Release();
            return std::unexpected("cannot write " + file + ": " + AvError(rc));
        }
    }
    AVDictionary *mux_opts = nullptr;
    if (opt.container == Container::Mp4)
    {
        av_dict_set(&mux_opts, "movflags", "+faststart", 0);
    }
    rc = avformat_write_header(d.fmt, &mux_opts);
    av_dict_free(&mux_opts);
    if (rc < 0)
    {
        d.Release();
        return std::unexpected("cannot write the header: " + AvError(rc));
    }
    d.header = true;

    d.frame         = av_frame_alloc();
    d.packet        = av_packet_alloc();
    d.frame->format = d.codec->pix_fmt;
    d.frame->width  = opt.width;
    d.frame->height = opt.height;
    rc              = av_frame_get_buffer(d.frame, 0);
    if (rc < 0)
    {
        d.Release();
        return std::unexpected("out of memory: " + AvError(rc));
    }
    d.sws = sws_getContext(opt.width, opt.height, AV_PIX_FMT_RGBA, opt.width, opt.height, d.codec->pix_fmt, SWS_BICUBIC, nullptr, nullptr,
                           nullptr);
    if (d.sws == nullptr)
    {
        d.Release();
        return std::unexpected("unsupported pixel format conversion");
    }
    d.width  = opt.width;
    d.height = opt.height;
    return {};
}

std::expected<void, std::string> VideoEncoder::AddFrame(std::span<const uint8_t> rgba)
{
    Impl &d = *_impl;
    if (d.codec == nullptr || d.finished)
    {
        return std::unexpected("the encoder is not open");
    }
    if (rgba.size() != static_cast<size_t>(d.width) * static_cast<size_t>(d.height) * 4)
    {
        return std::unexpected("frame size does not match the video size");
    }
    const int rc = av_frame_make_writable(d.frame);
    if (rc < 0)
    {
        return std::unexpected("out of memory: " + AvError(rc));
    }
    const std::array<const uint8_t *, 1> src{rgba.data()};
    const std::array<int, 1>             stride{d.width * 4};
    sws_scale(d.sws, src.data(), stride.data(), 0, d.height, d.frame->data, d.frame->linesize);
    d.frame->pts = d.next_pts++;
    return d.Encode(d.frame);
}

std::expected<void, std::string> VideoEncoder::Finish()
{
    Impl &d = *_impl;
    if (d.finished)
    {
        return {};
    }
    if (d.codec == nullptr)
    {
        return std::unexpected("the encoder is not open");
    }
    d.finished = true;
    if (auto r = d.Encode(nullptr); !r.has_value())
    {
        d.Release();
        return r;
    }
    const int rc = av_write_trailer(d.fmt);
    d.Release();
    if (rc < 0)
    {
        return std::unexpected("cannot finish the video: " + AvError(rc));
    }
    return {};
}

void VideoEncoder::Abort()
{
    _impl->finished = true;
    _impl->Release();
}

const std::string &VideoEncoder::EncoderName() const { return _impl->encoder; }

int VideoEncoder::FrameCount() const { return static_cast<int>(_impl->next_pts); }

#else // !AD_HAVE_LIBAV

struct VideoEncoder::Impl
{
    std::string encoder;
};

bool Available() { return false; }

std::string LibraryVersions() { return {}; }

std::vector<std::string> EncodersFor(Container /*c*/) { return {}; }

VideoEncoder::VideoEncoder() : _impl(std::make_unique<Impl>()) {}

VideoEncoder::~VideoEncoder() = default;

std::expected<void, std::string> VideoEncoder::Open(const std::filesystem::path & /*path*/, const VideoOptions & /*opt*/)
{
    return std::unexpected("built without libav: WebM / MP4 export is unavailable");
}

std::expected<void, std::string> VideoEncoder::AddFrame(std::span<const uint8_t> /*rgba*/)
{
    return std::unexpected("built without libav");
}

std::expected<void, std::string> VideoEncoder::Finish() { return std::unexpected("built without libav"); }

void VideoEncoder::Abort() {}

const std::string &VideoEncoder::EncoderName() const { return _impl->encoder; }

int VideoEncoder::FrameCount() const { return 0; }

#endif

} // namespace ad::video
