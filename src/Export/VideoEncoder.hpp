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
/// @brief In-process WebM / MP4 encoding with libavcodec + libavformat + libswscale.
///
/// The encoder is chosen at runtime from what the linked libav provides:
/// WebM -- libvpx-vp9, libvpx (VP8), libaom-av1, libsvtav1;
/// MP4  -- libx264, libopenh264, h264_mf (Windows), mpeg4 (always built in).
/// Without libav (WITH_LIBAV=OFF) every call reports an error.

namespace ad::video
{

enum class Container
{
    WebM,
    Mp4
};

struct VideoOptions
{
    int       width     = 0; // even
    int       height    = 0; // even
    double    fps       = 15;
    Container container = Container::Mp4;
    int       quality   = 2; // 0 (smallest file) .. 4 (best quality)
};

/// True when the application was built with libav.
bool Available();
/// libav version string ("libavcodec 60.31.102, libavformat ..."), empty without libav.
std::string LibraryVersions();
/// Encoders usable for the container, in preference order (empty: the container is unsupported).
std::vector<std::string> EncodersFor(Container c);

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
    /// Flush the encoder and write the trailer. Safe to call twice.
    std::expected<void, std::string> Finish();
    /// Close without finishing (cancelled export); the partial file stays on disk.
    void Abort();

    [[nodiscard]] const std::string &EncoderName() const;
    [[nodiscard]] int                FrameCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace ad::video

#endif // _EXPORT_VIDEO_ENCODER_HPP_
