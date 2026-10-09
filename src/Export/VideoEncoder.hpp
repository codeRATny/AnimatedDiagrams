#ifndef _EXPORT_VIDEO_ENCODER_HPP_
#define _EXPORT_VIDEO_ENCODER_HPP_

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

/// @file VideoEncoder.hpp
/// @brief In-process GIF / WebM / MP4 encoding with libavcodec + libavformat + libavfilter + libswscale.
///
/// The encoder is chosen at runtime from what the linked libav provides:
/// WebM -- libvpx-vp9, libvpx (VP8), libaom-av1, libsvtav1;
/// MP4  -- libx264, libopenh264, h264_mf (Windows), mpeg4 (always built in);
/// GIF  -- gif encoder + gif muxer, one palette for the whole animation from the
///         palettegen / paletteuse filters (two passes over the frames, see PassCount()).
/// Without libav (WITH_LIBAV=OFF) every call reports an error.

namespace ad::video
{

enum class Container
{
    WebM,
    Mp4,
    Gif
};

struct VideoOptions
{
    int       width     = 0; // WebM / MP4: even
    int       height    = 0; // WebM / MP4: even
    double    fps       = 15;
    Container container = Container::Mp4;
    int       quality   = 2;    // WebM / MP4: 0 (smallest file) .. 4 (best quality)
    bool      loop      = true; // GIF: play forever (NETSCAPE2.0 extension)
};

/// True when the application was built with libav.
bool Available();
/// libav version string ("libavcodec 60.31.102, libavformat ..."), empty without libav.
std::string LibraryVersions();
/// Encoders usable for the container, in preference order (empty: the container is unsupported).
std::vector<std::string> EncodersFor(Container c);
/// How many times the frames are passed to the encoder: 2 for GIF (palette analysis, then
/// encoding with that palette -- frames are not kept in memory), 1 for WebM / MP4.
int PassCount(Container c);

/// Usage: Open(); for every pass (NextPass() between passes) AddFrame() for all frames; Finish().
class VideoEncoder
{
public:
    VideoEncoder();
    ~VideoEncoder();
    VideoEncoder(const VideoEncoder &)            = delete;
    VideoEncoder &operator=(const VideoEncoder &) = delete;

    /// Create the file and pick the first encoder that opens with these options.
    std::expected<void, std::string> Open(const std::filesystem::path &path, const VideoOptions &opt);
    /// rgba.size() must be width * height * 4 (RGBA8888, row-major, no padding).
    std::expected<void, std::string> AddFrame(std::span<const uint8_t> rgba);
    /// Start the next pass (GIF: after every frame was added for the palette, add the same frames again).
    std::expected<void, std::string> NextPass();
    /// Flush the encoder and write the trailer (in the last pass). Safe to call twice.
    std::expected<void, std::string> Finish();
    /// Close without finishing (cancelled export); the partial file stays on disk.
    void Abort();

    [[nodiscard]] const std::string &EncoderName() const;
    /// Frames added in the current pass.
    [[nodiscard]] int FrameCount() const;
    /// Current pass, 0 .. PassCount() - 1.
    [[nodiscard]] int Pass() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace ad::video

#endif // _EXPORT_VIDEO_ENCODER_HPP_
