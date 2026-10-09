#include "VideoEncoder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "Utils/File.hpp"

#ifdef AD_HAVE_LIBAV
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#endif

namespace ad::video
{

int PassCount(Container c) { return c == Container::Gif ? 2 : 1; }

#ifdef AD_HAVE_LIBAV

namespace
{

/// GIF frame delays are in 1/100 s.
constexpr AVRational kGifTimeBase{1, 100};

/// Palette mapping: error diffusion; only the changed rectangle of a frame is re-dithered,
/// so static parts keep identical pixels and the gif encoder stores just the difference.
constexpr const char *kPaletteUseArgs = "dither=sierra2_4a:diff_mode=rectangle";

/// Candidate encoders in preference order.
std::vector<const char *> Candidates(Container c)
{
    switch (c)
    {
    case Container::WebM:
        return {"libvpx-vp9", "libvpx", "libaom-av1", "libsvtav1"};
    case Container::Gif:
        return {"gif"};
    case Container::Mp4:
        break;
    }
    return {"libx264", "libopenh264", "h264_mf", "mpeg4"};
}

const char *MuxerName(Container c)
{
    switch (c)
    {
    case Container::WebM:
        return "webm";
    case Container::Gif:
        return "gif";
    case Container::Mp4:
        break;
    }
    return "mp4";
}

std::string AvError(int code)
{
    std::array<char, 256> buf{};
    av_strerror(code, buf.data(), buf.size());
    return {buf.data()};
}

/// Presentation time of frame `index` in 1/100 s: no accumulated rounding error, but at least
/// 2/100 s per frame (browsers stretch shorter GIF delays to 1/10 s).
int64_t GifPts(int64_t index, int64_t previous, double fps)
{
    const auto exact = static_cast<int64_t>(std::llround(static_cast<double>(index) * 100.0 / fps));
    return index == 0 ? 0 : std::max(exact, previous + 2);
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
    else if (name != "gif") // libopenh264, h264_mf and others: plain bitrate
    {
        c->bit_rate = bitrate;
    }
}

/// The filters of the GIF palette passes are part of every regular libavfilter build,
/// but a trimmed build may lack them.
bool HasGifFilters()
{
    return std::ranges::all_of(std::array{"buffer", "buffersink", "palettegen", "paletteuse"},
                               [](const char *name)
                               {
                                   return avfilter_get_by_name(name) != nullptr;
                               });
}

/// Create a filter instance in `graph`; nullptr if the filter is missing or rejects the arguments.
AVFilterContext *CreateFilter(AVFilterGraph *graph, const char *filter, const char *name, const std::string &args)
{
    AVFilterContext *ctx = nullptr;
    const AVFilter  *f   = avfilter_get_by_name(filter);
    if (f == nullptr || avfilter_graph_create_filter(&ctx, f, name, args.empty() ? nullptr : args.c_str(), nullptr, graph) < 0)
    {
        return nullptr;
    }
    return ctx;
}

std::string BufferArgs(int width, int height, AVPixelFormat fmt)
{
    return "video_size=" + std::to_string(width) + "x" + std::to_string(height) + ":pix_fmt=" + std::to_string(static_cast<int>(fmt)) +
           ":time_base=" + std::to_string(kGifTimeBase.num) + "/" + std::to_string(kGifTimeBase.den) + ":pixel_aspect=1/1";
}

} // namespace

struct VideoEncoder::Impl
{
    AVFormatContext *fmt    = nullptr;
    AVCodecContext  *codec  = nullptr;
    AVStream        *stream = nullptr;
    AVFrame         *frame  = nullptr; // input frame (video: encoder format; GIF: RGBA)
    AVPacket        *packet = nullptr;
    SwsContext      *sws    = nullptr;
    // GIF: pass 0 -- frames -> palettegen; pass 1 -- frames + palette -> paletteuse -> encoder
    AVFilterGraph   *graph    = nullptr;
    AVFilterContext *src      = nullptr;
    AVFilterContext *sink     = nullptr;
    AVFrame         *palette  = nullptr;
    AVFrame         *filtered = nullptr;
    std::string      encoder;
    Container        container = Container::Mp4;
    double           fps       = 15;
    int              width     = 0;
    int              height    = 0;
    int              pass      = 0;
    int              frames    = 0; // frames added in the current pass
    int64_t          last_pts  = 0;
    bool             header    = false;
    bool             finished  = false;

    ~Impl() { Release(); }
    Impl()                        = default;
    Impl(const Impl &)            = delete;
    Impl &operator=(const Impl &) = delete;

    void Reset()
    {
        Release();
        encoder.clear();
        container = Container::Mp4;
        fps       = 15;
        width     = 0;
        height    = 0;
        pass      = 0;
        frames    = 0;
        last_pts  = 0;
        header    = false;
        finished  = false;
    }

    void ReleaseGraph()
    {
        avfilter_graph_free(&graph);
        src  = nullptr;
        sink = nullptr;
    }

    void Release()
    {
        ReleaseGraph();
        av_frame_free(&palette);
        av_frame_free(&filtered);
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
                return std::unexpected("cannot write the file: " + AvError(rc));
            }
        }
    }

    /// GIF pass 0: RGBA frames -> palettegen (statistics of all frames) -> sink.
    std::expected<void, std::string> BuildPaletteGraph()
    {
        graph                        = avfilter_graph_alloc();
        src                          = CreateFilter(graph, "buffer", "in", BufferArgs(width, height, AV_PIX_FMT_RGBA));
        AVFilterContext *palette_gen = CreateFilter(graph, "palettegen", "palettegen", "stats_mode=full");
        sink                         = CreateFilter(graph, "buffersink", "out", {});
        if (src == nullptr || palette_gen == nullptr || sink == nullptr || avfilter_link(src, 0, palette_gen, 0) < 0 ||
            avfilter_link(palette_gen, 0, sink, 0) < 0)
        {
            return std::unexpected(std::string("libavfilter: cannot create the palettegen filter"));
        }
        if (const int rc = avfilter_graph_config(graph, nullptr); rc < 0)
        {
            return std::unexpected("libavfilter: " + AvError(rc));
        }
        return {};
    }

    /// GIF pass 1: RGBA frames + the palette -> paletteuse -> sink (PAL8 frames for the encoder).
    std::expected<void, std::string> BuildPaletteUseGraph()
    {
        graph                        = avfilter_graph_alloc();
        src                          = CreateFilter(graph, "buffer", "in", BufferArgs(width, height, AV_PIX_FMT_RGBA));
        AVFilterContext *pal_src     = CreateFilter(graph, "buffer", "palette",
                                                    BufferArgs(palette->width, palette->height, static_cast<AVPixelFormat>(palette->format)));
        AVFilterContext *palette_use = CreateFilter(graph, "paletteuse", "paletteuse", kPaletteUseArgs);
        sink                         = CreateFilter(graph, "buffersink", "out", {});
        if (src == nullptr || pal_src == nullptr || palette_use == nullptr || sink == nullptr ||
            avfilter_link(src, 0, palette_use, 0) < 0 || avfilter_link(pal_src, 0, palette_use, 1) < 0 ||
            avfilter_link(palette_use, 0, sink, 0) < 0)
        {
            return std::unexpected(std::string("libavfilter: cannot create the paletteuse filter"));
        }
        int rc = avfilter_graph_config(graph, nullptr);
        if (rc < 0)
        {
            return std::unexpected("libavfilter: " + AvError(rc));
        }
        // the only palette frame, then end of the palette input: paletteuse keeps using it
        palette->pts = 0;
        rc           = av_buffersrc_add_frame_flags(pal_src, palette, AV_BUFFERSRC_FLAG_KEEP_REF);
        if (rc >= 0)
        {
            rc = av_buffersrc_add_frame(pal_src, nullptr);
        }
        if (rc < 0)
        {
            return std::unexpected("libavfilter: " + AvError(rc));
        }
        return {};
    }

    /// Encode every frame the paletteuse graph has ready (`eof`: until the end of the stream).
    std::expected<void, std::string> DrainPaletteUse(bool eof)
    {
        while (true)
        {
            const int rc = av_buffersink_get_frame(sink, filtered);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            {
                if (eof && rc == AVERROR(EAGAIN))
                {
                    return std::unexpected(std::string("libavfilter: paletteuse did not finish"));
                }
                return {};
            }
            if (rc < 0)
            {
                return std::unexpected("libavfilter: " + AvError(rc));
            }
            filtered->pts = av_rescale_q(filtered->pts, av_buffersink_get_time_base(sink), codec->time_base);
            auto r        = Encode(filtered);
            av_frame_unref(filtered);
            if (!r.has_value())
            {
                return r;
            }
        }
    }

    std::expected<void, std::string> OpenGifEncoder()
    {
        const AVCodec *gif = avcodec_find_encoder_by_name("gif");
        if (gif == nullptr || !HasGifFilters())
        {
            return std::unexpected(std::string("no GIF encoder or palette filters in libav"));
        }
        codec            = avcodec_alloc_context3(gif);
        codec->width     = width;
        codec->height    = height;
        codec->pix_fmt   = AV_PIX_FMT_PAL8;
        codec->time_base = kGifTimeBase;
        codec->framerate = av_d2q(fps, 100000);
        if (const int rc = avcodec_open2(codec, gif, nullptr); rc < 0)
        {
            return std::unexpected("cannot open the GIF encoder: " + AvError(rc));
        }
        encoder = "gif";
        return {};
    }

    std::expected<void, std::string> OpenVideoEncoder(const VideoOptions &opt)
    {
        // first encoder that opens wins (e.g. a stubbed openh264 falls through to mpeg4)
        std::string tried;
        for (const std::string &name : EncodersFor(opt.container))
        {
            const AVCodec  *candidate = avcodec_find_encoder_by_name(name.c_str());
            AVCodecContext *c         = avcodec_alloc_context3(candidate);
            c->width                  = opt.width;
            c->height                 = opt.height;
            c->framerate              = av_d2q(opt.fps, 100000);
            c->time_base              = av_inv_q(c->framerate);
            c->pix_fmt                = PickPixelFormat(candidate);
            c->gop_size               = std::max(1, static_cast<int>(std::lround(opt.fps * 2)));
            if ((fmt->oformat->flags & AVFMT_GLOBALHEADER) != 0)
            {
                c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }
            AVDictionary *codec_opts = nullptr;
            Configure(c, name, opt.quality, &codec_opts);
            const int rc = avcodec_open2(c, candidate, &codec_opts);
            av_dict_free(&codec_opts);
            if (rc >= 0)
            {
                codec   = c;
                encoder = name;
                return {};
            }
            tried += (tried.empty() ? "" : ", ") + name + " (" + AvError(rc) + ")";
            avcodec_free_context(&c);
        }
        return std::unexpected(std::string("no usable ") +
                               (opt.container == Container::WebM ? "WebM (VP9/VP8/AV1)" : "MP4 (H.264/MPEG-4)") + " encoder in libav" +
                               (tried.empty() ? std::string() : ": " + tried));
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
           ver("libavfilter", avfilter_version()) + ", " + ver("libswscale", swscale_version());
}

std::vector<std::string> EncodersFor(Container c)
{
    std::vector<std::string> out;
    const AVOutputFormat    *muxer = av_guess_format(MuxerName(c), nullptr, nullptr);
    if (muxer == nullptr || (c == Container::Gif && !HasGifFilters()))
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
    const bool gif = opt.container == Container::Gif;
    if (gif ? opt.width <= 0 || opt.height <= 0 || opt.width > 65535 || opt.height > 65535
            : opt.width <= 0 || opt.height <= 0 || opt.width % 2 != 0 || opt.height % 2 != 0)
    {
        return std::unexpected(gif ? "GIF size must be 1..65535 pixels" : "video size must be positive and even");
    }
    if (opt.fps <= 0 || opt.fps > 240)
    {
        return std::unexpected("invalid frame rate");
    }
    av_log_set_level(AV_LOG_ERROR);
    d.container = opt.container;
    d.fps       = opt.fps;
    d.width     = opt.width;
    d.height    = opt.height;

    const std::string file = PathToUtf8(path);
    int               rc   = avformat_alloc_output_context2(&d.fmt, nullptr, MuxerName(opt.container), file.c_str());
    if (rc < 0 || d.fmt == nullptr)
    {
        return std::unexpected("cannot create the container: " + AvError(rc));
    }
    if (auto r = gif ? d.OpenGifEncoder() : d.OpenVideoEncoder(opt); !r.has_value())
    {
        d.Release();
        return r;
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
    else if (gif)
    {
        av_dict_set_int(&mux_opts, "loop", opt.loop ? 0 : -1, 0); // 0: forever, -1: no NETSCAPE2.0 extension
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
    d.frame->format = gif ? AV_PIX_FMT_RGBA : d.codec->pix_fmt;
    d.frame->width  = opt.width;
    d.frame->height = opt.height;
    rc              = av_frame_get_buffer(d.frame, 0);
    if (rc < 0)
    {
        d.Release();
        return std::unexpected("out of memory: " + AvError(rc));
    }
    if (gif)
    {
        d.palette  = av_frame_alloc();
        d.filtered = av_frame_alloc();
        if (auto r = d.BuildPaletteGraph(); !r.has_value())
        {
            d.Release();
            return r;
        }
        return {};
    }
    d.sws = sws_getContext(opt.width, opt.height, AV_PIX_FMT_RGBA, opt.width, opt.height, d.codec->pix_fmt, SWS_BICUBIC, nullptr, nullptr,
                           nullptr);
    if (d.sws == nullptr)
    {
        d.Release();
        return std::unexpected("unsupported pixel format conversion");
    }
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
    int rc = av_frame_make_writable(d.frame);
    if (rc < 0)
    {
        return std::unexpected("out of memory: " + AvError(rc));
    }
    if (d.container != Container::Gif)
    {
        const std::array<const uint8_t *, 1> src{rgba.data()};
        const std::array<int, 1>             stride{d.width * 4};
        sws_scale(d.sws, src.data(), stride.data(), 0, d.height, d.frame->data, d.frame->linesize);
        d.frame->pts = d.frames++;
        return d.Encode(d.frame);
    }

    const auto row = static_cast<size_t>(d.width) * 4;
    for (int y = 0; y < d.height; ++y)
    {
        std::memcpy(d.frame->data[0] + static_cast<ptrdiff_t>(y) * d.frame->linesize[0], rgba.data() + row * static_cast<size_t>(y), row);
    }
    d.last_pts   = GifPts(d.frames, d.last_pts, d.fps);
    d.frame->pts = d.last_pts;
    ++d.frames;
    rc = av_buffersrc_add_frame_flags(d.src, d.frame, AV_BUFFERSRC_FLAG_KEEP_REF | AV_BUFFERSRC_FLAG_PUSH); // processed now, not queued
    if (rc < 0)
    {
        return std::unexpected("libavfilter: " + AvError(rc));
    }
    return d.pass == 0 ? std::expected<void, std::string>{} : d.DrainPaletteUse(false);
}

std::expected<void, std::string> VideoEncoder::NextPass()
{
    Impl &d = *_impl;
    if (d.codec == nullptr || d.finished)
    {
        return std::unexpected("the encoder is not open");
    }
    if (d.pass + 1 >= PassCount(d.container))
    {
        return std::unexpected("this format is encoded in a single pass");
    }
    // end of the palette pass: palettegen emits the palette of all frames
    int rc = av_buffersrc_add_frame(d.src, nullptr);
    if (rc >= 0)
    {
        rc = av_buffersink_get_frame(d.sink, d.palette);
    }
    if (rc < 0)
    {
        return std::unexpected(d.frames == 0 ? std::string("no frames to encode") : "palette generation failed: " + AvError(rc));
    }
    d.ReleaseGraph();
    if (auto r = d.BuildPaletteUseGraph(); !r.has_value())
    {
        return r;
    }
    ++d.pass;
    d.frames   = 0;
    d.last_pts = 0;
    return {};
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
    if (d.pass + 1 < PassCount(d.container))
    {
        return std::unexpected("all passes over the frames are required (NextPass)");
    }
    d.finished = true;
    if (d.container == Container::Gif)
    {
        const int rc = av_buffersrc_add_frame(d.src, nullptr);
        if (rc < 0)
        {
            d.Release();
            return std::unexpected("libavfilter: " + AvError(rc));
        }
        if (auto r = d.DrainPaletteUse(true); !r.has_value())
        {
            d.Release();
            return r;
        }
    }
    if (auto r = d.Encode(nullptr); !r.has_value())
    {
        d.Release();
        return r;
    }
    const int rc = av_write_trailer(d.fmt);
    d.Release();
    if (rc < 0)
    {
        return std::unexpected("cannot finish the file: " + AvError(rc));
    }
    return {};
}

void VideoEncoder::Abort()
{
    _impl->finished = true;
    _impl->Release();
}

const std::string &VideoEncoder::EncoderName() const { return _impl->encoder; }

int VideoEncoder::FrameCount() const { return _impl->frames; }

int VideoEncoder::Pass() const { return _impl->pass; }

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
    return std::unexpected("built without libav: GIF / WebM / MP4 export is unavailable");
}

std::expected<void, std::string> VideoEncoder::AddFrame(std::span<const uint8_t> /*rgba*/)
{
    return std::unexpected("built without libav");
}

std::expected<void, std::string> VideoEncoder::NextPass() { return std::unexpected("built without libav"); }

std::expected<void, std::string> VideoEncoder::Finish() { return std::unexpected("built without libav"); }

void VideoEncoder::Abort() {}

const std::string &VideoEncoder::EncoderName() const { return _impl->encoder; }

int VideoEncoder::FrameCount() const { return 0; }

int VideoEncoder::Pass() const { return 0; }

#endif

} // namespace ad::video
