#ifndef _EXPORT_PPTX_HPP_
#define _EXPORT_PPTX_HPP_

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Engine/DisplayList.hpp"
#include "Model/Color.hpp"
#include "Model/Model.hpp"
#include "Model/Registry.hpp"

/// @file Pptx.hpp
/// @brief PowerPoint (.pptx) export: new presentations from the built-in template or slides
///        inserted into an existing presentation.
///
/// Slide kinds:
///  - media: a rendered video (autoplay, loop) or animated GIF per slide (rendering is done by the
///    caller -- the core has no pixel renderer);
///  - animated: editable shapes (nodes, edges, labels) with PowerPoint animations (appear /
///    disappear, motion paths for messages, emphasis effects);
///  - morph: key frames as slides with the Morph transition (PowerPoint 2019 / 365).
/// The container is handled by libzip, the XML parts by pugixml.

namespace ad::pptx
{

struct SlideSize
{
    int64_t     cx                               = 12192000; // EMU (914400 per inch)
    int64_t     cy                               = 6858000;
    friend bool operator==(SlideSize, SlideSize) = default;
};
inline constexpr SlideSize kWide{12192000, 6858000};    // 16:9
inline constexpr SlideSize kStandard{9144000, 6858000}; // 4:3

/// Where the slides go.
struct Target
{
    std::filesystem::path output;            // file written (may equal insert_into)
    std::filesystem::path insert_into;       // existing presentation; empty -- a new one
    int                   insert_after = -1; // new slides after this 1-based slide (0 -- first, -1 -- at the end)
    SlideSize             size;              // slide size of a new presentation
    std::string           title;             // document title of a new presentation
};

/// One rendered clip: a slide with a video (.mp4) or an animated .gif.
struct MediaSlide
{
    std::filesystem::path media;
    std::filesystem::path poster;          // first frame (.png), videos only
    int                   width       = 0; // pixels: aspect ratio of the picture
    int                   height      = 0;
    double                duration_ms = 0;
    bool                  loop        = true;
    Color                 background;
};

/// Slides with rendered clips, played automatically when the slide is shown. Throws ad::IoError.
void WriteMediaSlides(const Target &target, std::span<const MediaSlide> slides);

struct VectorOptions
{
    enum class Mode
    {
        Animated, // shapes + PowerPoint animations
        Morph     // key frames with the Morph transition
    };
    Mode mode = Mode::Animated;
    /// [start, end) in ms, one slide (animated) or one group of key frames (morph) each;
    /// empty -- the whole scenario. Slides advance on click between segments.
    std::vector<std::pair<double, double>> segments;
    double                                 morph_step_ms = 400;  // key frame interval (morph)
    double                                 margin        = 0.04; // fraction of the slide size
};

/// Slides with native, editable shapes. Throws ad::IoError.
void WriteVectorSlides(const Target &target, const Model &m, const Registry &reg, const TextMeasurer &tm, const VectorOptions &opt);

/// What a presentation contains (tests, diagnostics).
struct DeckInfo
{
    int                      slides = 0;
    SlideSize                size;
    std::vector<std::string> media;          // part names
    int                      shapes     = 0; // p:sp / p:pic / p:cxnSp / p:grpSp on all slides
    int                      animations = 0; // effect nodes (presetClass) on all slides
    int                      morph      = 0; // slides with the Morph transition
    int                      autoplay   = 0; // media nodes started automatically
};
DeckInfo Inspect(const std::filesystem::path &path);

} // namespace ad::pptx

#endif // _EXPORT_PPTX_HPP_
