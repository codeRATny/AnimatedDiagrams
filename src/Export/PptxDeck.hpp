#ifndef _EXPORT_PPTX_DECK_HPP_
#define _EXPORT_PPTX_DECK_HPP_

#include <pugixml.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Export/Opc.hpp"
#include "Export/Pptx.hpp"
#include "Model/Color.hpp"

/// @file PptxDeck.hpp
/// @brief Internal building blocks of the PowerPoint export: the presentation package
///        (slides, layouts, media) and the animation timeline of a slide.

namespace ad::pptx
{

/// Template presentation (python-pptx default, created by PowerPoint; MIT).
std::span<const uint8_t> TemplateBytes();

/// Append an XML fragment (elements with a:/p:/r: prefixes) to `parent`; returns the first new node.
pugi::xml_node AppendXml(pugi::xml_node parent, std::string_view fragment);

/// "rrggbb" for <a:srgbClr val="..."/>.
std::string Hex(Color c);

class Deck;

/// A slide being written.
class Slide
{
public:
    [[nodiscard]] pugi::xml_node     Tree() const { return _tree; } // p:spTree
    [[nodiscard]] pugi::xml_node     Root() const { return _doc.document_element(); }
    [[nodiscard]] int                NextId() { return _next_id++; }
    [[nodiscard]] const std::string &Part() const { return _part; }

    void SetBackground(Color c);
    /// Copy a file into ppt/media and reference it from this slide; returns {relationship id, part}.
    std::string AddImage(const std::filesystem::path &file);
    /// Video relationships: {r:link id (video), r:embed id (media)}.
    std::pair<std::string, std::string> AddVideo(const std::filesystem::path &file);
    /// Morph transition from the previous slide; `advance_ms` >= 0 advances automatically.
    void SetMorphTransition(double duration_ms, std::optional<double> advance_ms);
    /// <p:timing> element (created on first use, placed after the transition).
    pugi::xml_node Timing();

private:
    friend class Deck;
    Slide(Deck &deck, std::string part);

    Deck              *_deck;
    std::string        _part;
    pugi::xml_document _doc;
    pugi::xml_node     _tree;
    int                _next_id = 2;
};

/// The presentation being written: a new one from the template or an existing one.
class Deck
{
public:
    explicit Deck(const Target &target);

    [[nodiscard]] SlideSize Size() const { return _size; }
    /// A new blank slide at the target position.
    Slide &AddSlide();
    /// Write all slides and the presentation part; commits the package.
    void Finish();

    opc::Package &Package() { return _pkg; }

private:
    void                      _LoadPresentation();
    [[nodiscard]] std::string _BlankLayout();

    Target                              _target;
    opc::Package                        _pkg;
    pugi::xml_document                  _pres;
    SlideSize                           _size;
    std::string                         _layout; // part of the layout used for new slides
    int                                 _insert_at = 0;
    std::vector<std::unique_ptr<Slide>> _slides;
};

/// Builds <p:timing>: one group started automatically when the slide is shown; every effect
/// starts "with previous" after its own delay.
class Timeline
{
public:
    explicit Timeline(Slide &slide);

    /// Plays a media shape from the start (loops when `loop`).
    void MediaPlay(int spid, double duration_ms, bool loop);
    /// Appear (fade when fade_ms > 0) at `delay_ms`.
    void Entrance(int spid, double delay_ms, double fade_ms = 0);
    /// Disappear (fade when fade_ms > 0) at `delay_ms`.
    void Exit(int spid, double delay_ms, double fade_ms = 0);
    /// Motion along `path` ("M 0 0 L dx dy ..." in fractions of the slide size, relative to the shape).
    void Motion(int spid, double delay_ms, double duration_ms, const std::string &path, int points);
    /// Grow / shrink and back (`percent` of the size), `repeat` times.
    void Pulse(int spid, double delay_ms, double duration_ms, int percent, int repeat);
    /// Rotation by `degrees` and back (teeter) or full turns (spin, `back` false).
    void Rotate(int spid, double delay_ms, double duration_ms, double degrees, bool back, int repeat);
    /// Visibility blinking.
    void Blink(int spid, double delay_ms, double duration_ms, int repeat);

    [[nodiscard]] int Effects() const { return _effects; }

private:
    pugi::xml_node    _Effect(int preset_id, std::string_view preset_class, int subtype, double delay_ms);
    [[nodiscard]] int _Id() { return _next_id++; }

    Slide         &_slide;
    pugi::xml_node _group; // children: one p:par per effect
    int            _next_id = 1;
    int            _effects = 0;
};

/// EMU per inch / point.
inline constexpr double kEmuPerInch = 914400;
inline constexpr double kEmuPerPt   = 12700;

} // namespace ad::pptx

#endif // _EXPORT_PPTX_DECK_HPP_
