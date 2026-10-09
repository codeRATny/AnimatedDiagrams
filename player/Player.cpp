// HTML player engine: the Qt-free core (ad_engine) compiled to WebAssembly.
//
// player.js loads a document with ad_player_load(), then asks for the display list of
// a moment of time with ad_player_build() and draws it on a <canvas>. The display list
// crosses the boundary as a flat Float64Array + UTF-8 string blob (Engine/FrameBuffer.hpp).
// Text is measured by the browser (CanvasRenderingContext2D.measureText) so labels,
// badges and layout use the same font metrics as the drawing.

#include <emscripten/emscripten.h>

#include <cstddef>
#include <exception>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Engine/FrameBuffer.hpp"
#include "Engine/Scene.hpp"
#include "Export/ExportPlan.hpp"
#include "Io/JsonIo.hpp"
#include "Model/Registry.hpp"

// Width of `text` at a 100 px font (bold: 0/1, family: "" -- the default font), measured by
// player.js (Module.adMeasureText). Falls back to an approximation when it is not provided.
// NOLINTBEGIN
EM_JS(double, ad_js_measure_text, (const char *text, int bold, const char *family), {
    var fn = Module['adMeasureText'];
    var s  = UTF8ToString(text);
    return fn ? fn(s, bold != 0, UTF8ToString(family)) : 60 * s.length;
});
// NOLINTEND

namespace
{

using namespace ad;

constexpr double kRefPx = 100; // measurement font size; widths scale linearly

/// Browser text metrics with a cache: the scene measures the same labels every frame.
class CanvasTextMeasurer final : public TextMeasurer
{
public:
    [[nodiscard]] double Width(std::string_view utf8, const Font &font) const override
    {
        std::string key;
        key.reserve(utf8.size() + font.family.size() + 2);
        key += font.bold ? 'b' : 'r';
        key += font.family;
        key += '\x1f';
        key += utf8;
        auto it = _cache.find(key);
        if (it == _cache.end())
        {
            const std::string text(utf8);
            it = _cache.emplace(std::move(key), ad_js_measure_text(text.c_str(), font.bold ? 1 : 0, font.family.c_str())).first;
        }
        return it->second * font.size / kRefPx;
    }

    void Clear() { _cache.clear(); }

private:
    mutable std::unordered_map<std::string, double> _cache;
};

struct PlayerState
{
    Model              model;
    bool               loaded = false;
    std::string        error;
    std::string        background;
    Rect               bounds;
    FrameBuffer        frame;
    CanvasTextMeasurer measurer;
};

PlayerState &State()
{
    static PlayerState state;
    return state;
}

void UpdateBounds(PlayerState &s)
{
    // the same framing as the application's export ("fit to content")
    s.bounds = ContentBounds(s.model, s.measurer, Registry::Default()).Adjusted(kExportPadding);
}

} // namespace

extern "C"
{

    /// Load a document (UTF-8 JSON of `len` bytes). 1 on success, 0 on error (see ad_player_error).
    EMSCRIPTEN_KEEPALIVE int ad_player_load(const char *json, size_t len)
    {
        PlayerState &s = State();
        try
        {
            auto model = ParseModel(std::string_view(json, len));
            if (!model.has_value())
            {
                s.error = model.error();
                return 0;
            }
            s.model      = std::move(*model);
            s.background = s.model.scene.background;
            s.loaded     = true;
            s.error.clear();
            s.measurer.Clear();
            UpdateBounds(s);
            return 1;
        }
        catch (const std::exception &ex)
        {
            s.error = ex.what();
            return 0;
        }
    }

    EMSCRIPTEN_KEEPALIVE const char *ad_player_error() { return State().error.c_str(); }

    /// Scene duration in milliseconds.
    EMSCRIPTEN_KEEPALIVE double ad_player_duration() { return State().loaded ? State().model.scenario.duration : 0; }

    /// Background color "#rrggbb".
    EMSCRIPTEN_KEEPALIVE const char *ad_player_background() { return State().background.c_str(); }

    /// World rectangle to show (content bounds + export padding): x, y, w, h.
    EMSCRIPTEN_KEEPALIVE double ad_player_bounds(int index)
    {
        const Rect &r = State().bounds;
        switch (index)
        {
        case 0:
            return r.x;
        case 1:
            return r.y;
        case 2:
            return r.w;
        default:
            return r.h;
        }
    }

    /// Forget cached text widths (after web fonts load) and recompute the framing.
    EMSCRIPTEN_KEEPALIVE void ad_player_fonts_changed()
    {
        PlayerState &s = State();
        s.measurer.Clear();
        if (s.loaded)
        {
            UpdateBounds(s);
        }
    }

    /// Build the frame at `t_ms`; returns the number of doubles in ad_player_frame().
    EMSCRIPTEN_KEEPALIVE int ad_player_build(double t_ms)
    {
        PlayerState &s = State();
        if (!s.loaded)
        {
            return 0;
        }
        SceneOptions opt;
        opt.editor_chrome = false;
        s.frame.Encode(BuildFrame(s.model, t_ms, opt, s.measurer, Registry::Default()));
        return static_cast<int>(s.frame.Data().size());
    }

    EMSCRIPTEN_KEEPALIVE const double *ad_player_frame() { return State().frame.Data().data(); }
    EMSCRIPTEN_KEEPALIVE const char   *ad_player_strings() { return State().frame.Strings().data(); }
    EMSCRIPTEN_KEEPALIVE int           ad_player_strings_size() { return static_cast<int>(State().frame.Strings().size()); }

} // extern "C"
