#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <variant>

#include "Engine/Engine.hpp"
#include "Engine/Scene.hpp"
#include "Export/Pptx.hpp"
#include "Export/PptxDeck.hpp"
#include "Model/Catalog.hpp"

/// @file PptxVector.cpp
/// @brief Display list -> DrawingML shapes; "animated" slides (PowerPoint effects) and
///        "morph" slides (key frames).

namespace ad::pptx
{

namespace
{

// ---------------------------------------------------------------------------
// Geometry: scene pixels -> slide EMU
// ---------------------------------------------------------------------------

struct Mapping
{
    double s  = 1; // EMU per scene pixel
    double ox = 0;
    double oy = 0;
    double sw = 1; // slide size (EMU) for motion path fractions
    double sh = 1;

    [[nodiscard]] Vec2   Map(Vec2 p) const { return {ox + p.x * s, oy + p.y * s}; }
    [[nodiscard]] double Len(double px) const { return px * s; }
};

Mapping MakeMapping(const Rect &content, SlideSize size, double margin)
{
    Mapping      m;
    const double w = static_cast<double>(size.cx) * (1 - 2 * margin);
    const double h = static_cast<double>(size.cy) * (1 - 2 * margin);
    m.s            = std::min(w / std::max(1.0, content.w), h / std::max(1.0, content.h));
    m.ox           = (static_cast<double>(size.cx) - content.w * m.s) / 2 - content.x * m.s;
    m.oy           = (static_cast<double>(size.cy) - content.h * m.s) / 2 - content.y * m.s;
    m.sw           = static_cast<double>(size.cx);
    m.sh           = static_cast<double>(size.cy);
    return m;
}

int64_t E(double v) { return static_cast<int64_t>(std::llround(v)); }

Vec2 Apply(const std::optional<Transform> &t, Vec2 p)
{
    if (!t.has_value())
    {
        return p;
    }
    const double a = t->rotate_deg * std::numbers::pi / 180;
    const Vec2   d{(p.x - t->origin.x) * t->scale, (p.y - t->origin.y) * t->scale};
    return {t->origin.x + d.x * std::cos(a) - d.y * std::sin(a) + t->translate.x,
            t->origin.y + d.x * std::sin(a) + d.y * std::cos(a) + t->translate.y};
}

/// Circular arc as cubic segments (<= 90 degrees each).
Path ArcPath(const ArcShape &a)
{
    Path         p;
    const int    n    = std::max(1, static_cast<int>(std::ceil(std::abs(a.sweep_deg) / 90)));
    const double step = a.sweep_deg / n * std::numbers::pi / 180;
    double       t0   = a.start_deg * std::numbers::pi / 180;
    auto         pt   = [&](double t)
    {
        return Vec2{a.center.x + a.radius * std::cos(t), a.center.y + a.radius * std::sin(t)};
    };
    p.MoveTo(pt(t0));
    const double k = 4.0 / 3 * std::tan(step / 4);
    for (int i = 0; i < n; ++i)
    {
        const double t1 = t0 + step;
        const Vec2   p0 = pt(t0);
        const Vec2   p1 = pt(t1);
        const Vec2   c1{p0.x - k * a.radius * std::sin(t0), p0.y + k * a.radius * std::cos(t0)};
        const Vec2   c2{p1.x + k * a.radius * std::sin(t1), p1.y - k * a.radius * std::cos(t1)};
        p.CubicTo(c1, c2, p1);
        t0 = t1;
    }
    return p;
}

std::string Alpha(double opacity)
{
    const int v = static_cast<int>(std::lround(std::clamp(opacity, 0.0, 1.0) * 100000));
    return v >= 100000 ? std::string() : std::format(R"(<a:alpha val="{}"/>)", v);
}

std::string SolidFill(Color c, double opacity)
{
    return std::format(R"(<a:solidFill><a:srgbClr val="{}">{}</a:srgbClr></a:solidFill>)", Hex(c), Alpha(opacity));
}

std::string Line(const std::optional<Stroke> &st, double opacity, const Mapping &map)
{
    if (!st.has_value())
    {
        return "<a:ln><a:noFill/></a:ln>";
    }
    std::string dash;
    if (st->dash.size() >= 2 && st->width > 0)
    {
        dash = std::format(R"(<a:custDash><a:ds d="{}" sp="{}"/></a:custDash>)", E(st->dash[0] / st->width * 100000),
                           E(st->dash[1] / st->width * 100000));
    }
    return std::format(R"(<a:ln w="{}"{}>{}{}<a:round/></a:ln>)", std::max<int64_t>(1, E(map.Len(st->width))),
                       st->round_cap ? R"( cap="rnd")" : "", SolidFill(st->color, opacity), dash);
}

std::string Effects(const Paint &paint, const Mapping &map)
{
    const Color glow = paint.fill.value_or(paint.stroke.has_value() ? paint.stroke->color : Color::Rgb(0xffffff));
    switch (paint.effect)
    {
    case PaintEffect::Glow:
        return std::format(R"(<a:effectLst><a:glow rad="{}"><a:srgbClr val="{}"><a:alpha val="45000"/></a:srgbClr></a:glow></a:effectLst>)",
                           E(map.Len(8)), Hex(glow));
    case PaintEffect::Shadow:
        return std::format(R"(<a:effectLst><a:outerShdw blurRad="{}" dist="{}" dir="5400000" algn="t" rotWithShape="0">)"
                           R"(<a:srgbClr val="000000"><a:alpha val="35000"/></a:srgbClr></a:outerShdw></a:effectLst>)",
                           E(map.Len(10)), E(map.Len(4)));
    case PaintEffect::None:
        break;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------

struct Bounds
{
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    void   Add(Vec2 p)
    {
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
    [[nodiscard]] bool Valid() const { return x0 <= x1 && y0 <= y1; }
};

class ShapeWriter
{
public:
    ShapeWriter(Slide &slide, const Mapping &map, const TextMeasurer &tm) : _slide(slide), _map(map), _tm(tm) {}

    /// Writes one display list item into `parent`; returns its id (0 -- nothing written).
    int Item(pugi::xml_node parent, const ad::Item &item, const std::string &name, Bounds *bounds)
    {
        if (const auto *t = std::get_if<TextShape>(&item.shape))
        {
            return _Text(parent, *t, item, name, bounds);
        }
        Path path;
        if (const auto *r = std::get_if<RectShape>(&item.shape))
        {
            if (!item.transform.has_value())
            {
                return _Preset(parent, item, name, bounds, r->rect, r->radius > 0 ? "roundRect" : "rect",
                               r->radius > 0 ? std::min(50000.0, r->radius / std::max(1.0, std::min(r->rect.w, r->rect.h)) * 100000) : -1);
            }
            path = Path::RoundedRect(r->rect.x, r->rect.y, r->rect.w, r->rect.h, r->radius);
        }
        else if (const auto *e = std::get_if<EllipseShape>(&item.shape))
        {
            if (!item.transform.has_value())
            {
                return _Preset(parent, item, name, bounds, {e->center.x - e->rx, e->center.y - e->ry, 2 * e->rx, 2 * e->ry}, "ellipse", -1);
            }
            path = Path::Ellipse(e->center, e->rx, e->ry);
        }
        else if (const auto *p = std::get_if<PathShape>(&item.shape))
        {
            path = p->path;
        }
        else if (const auto *a = std::get_if<ArcShape>(&item.shape))
        {
            path = ArcPath(*a);
        }
        return _Custom(parent, item, name, bounds, path);
    }

    /// Group of items; returns its id (or the single item's id).
    int Group(pugi::xml_node parent, std::span<const ad::Item *const> items, const std::string &name)
    {
        if (items.size() == 1)
        {
            return Item(parent, *items[0], name, nullptr);
        }
        const int id = _slide.NextId();
        auto grp = AppendXml(parent, std::format(R"(<p:grpSp><p:nvGrpSpPr><p:cNvPr id="{}" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>)"
                                                 R"(<p:grpSpPr/></p:grpSp>)",
                                                 id));
        grp.child("p:nvGrpSpPr").child("p:cNvPr").attribute("name").set_value(name.c_str());
        Bounds b;
        int    written = 0;
        int    k       = 0;
        for (const ad::Item *it : items)
        {
            written += Item(grp, *it, std::format("{} {}", name, ++k), &b) != 0 ? 1 : 0;
        }
        if (written == 0 || !b.Valid())
        {
            parent.remove_child(grp);
            return 0;
        }
        const int64_t x = E(b.x0), y = E(b.y0), cx = std::max<int64_t>(1, E(b.x1 - b.x0)), cy = std::max<int64_t>(1, E(b.y1 - b.y0));
        AppendXml(grp.child("p:grpSpPr"), std::format(R"(<a:xfrm><a:off x="{0}" y="{1}"/><a:ext cx="{2}" cy="{3}"/>)"
                                                      R"(<a:chOff x="{0}" y="{1}"/><a:chExt cx="{2}" cy="{3}"/></a:xfrm>)",
                                                      x, y, cx, cy));
        return id;
    }

private:
    pugi::xml_node _Sp(pugi::xml_node parent, int id, const std::string &name, bool text_box)
    {
        auto sp =
            AppendXml(parent, std::format(R"(<p:sp><p:nvSpPr><p:cNvPr id="{}" name=""/><p:cNvSpPr{}/><p:nvPr/></p:nvSpPr><p:spPr/></p:sp>)",
                                          id, text_box ? R"( txBox="1")" : ""));
        sp.child("p:nvSpPr").child("p:cNvPr").attribute("name").set_value(name.c_str());
        return sp;
    }

    std::string _Fill(const Paint &paint) const { return paint.fill.has_value() ? SolidFill(*paint.fill, paint.opacity) : "<a:noFill/>"; }

    int _Preset(pugi::xml_node parent, const ad::Item &item, const std::string &name, Bounds *bounds, const Rect &r, std::string_view prst,
                double adj)
    {
        const Vec2 a = _map.Map({r.x, r.y});
        const Vec2 b = _map.Map({r.x + r.w, r.y + r.h});
        if (bounds != nullptr)
        {
            bounds->Add(a);
            bounds->Add(b);
        }
        const int  id = _slide.NextId();
        auto       sp = _Sp(parent, id, name, false);
        const auto av =
            adj >= 0 ? std::format(R"(<a:avLst><a:gd name="adj" fmla="val {}"/></a:avLst>)", E(adj)) : std::string("<a:avLst/>");
        AppendXml(
            sp.child("p:spPr"),
            std::format(R"(<a:xfrm><a:off x="{}" y="{}"/><a:ext cx="{}" cy="{}"/></a:xfrm><a:prstGeom prst="{}">{}</a:prstGeom>{}{}{})",
                        E(a.x), E(a.y), std::max<int64_t>(1, E(b.x - a.x)), std::max<int64_t>(1, E(b.y - a.y)), prst, av, _Fill(item.paint),
                        Line(item.paint.stroke, item.paint.opacity, _map), Effects(item.paint, _map)));
        return id;
    }

    int _Custom(pugi::xml_node parent, const ad::Item &item, const std::string &name, Bounds *bounds, const Path &path)
    {
        if (path.Segments().empty())
        {
            return 0;
        }
        std::vector<Path::Segment> segs = path.Segments();
        Bounds                     b;
        for (auto &s : segs)
        {
            s.c1 = _map.Map(Apply(item.transform, s.c1));
            s.c2 = _map.Map(Apply(item.transform, s.c2));
            s.to = _map.Map(Apply(item.transform, s.to));
            if (s.kind != Path::Kind::Close)
            {
                b.Add(s.to);
            }
            if (s.kind == Path::Kind::Quad || s.kind == Path::Kind::Cubic)
            {
                b.Add(s.c1);
            }
            if (s.kind == Path::Kind::Cubic)
            {
                b.Add(s.c2);
            }
        }
        if (!b.Valid())
        {
            return 0;
        }
        if (bounds != nullptr)
        {
            bounds->Add({b.x0, b.y0});
            bounds->Add({b.x1, b.y1});
        }
        const int64_t w  = std::max<int64_t>(1, E(b.x1 - b.x0));
        const int64_t h  = std::max<int64_t>(1, E(b.y1 - b.y0));
        auto          pt = [&](Vec2 p)
        {
            return std::format(R"(<a:pt x="{}" y="{}"/>)", E(p.x - b.x0), E(p.y - b.y0));
        };
        std::string cmds;
        for (const auto &s : segs)
        {
            switch (s.kind)
            {
            case Path::Kind::Move:
                cmds += "<a:moveTo>" + pt(s.to) + "</a:moveTo>";
                break;
            case Path::Kind::Line:
                cmds += "<a:lnTo>" + pt(s.to) + "</a:lnTo>";
                break;
            case Path::Kind::Quad:
                cmds += "<a:quadBezTo>" + pt(s.c1) + pt(s.to) + "</a:quadBezTo>";
                break;
            case Path::Kind::Cubic:
                cmds += "<a:cubicBezTo>" + pt(s.c1) + pt(s.c2) + pt(s.to) + "</a:cubicBezTo>";
                break;
            case Path::Kind::Close:
                cmds += "<a:close/>";
                break;
            }
        }
        const int id = _slide.NextId();
        auto      sp = _Sp(parent, id, name, false);
        AppendXml(
            sp.child("p:spPr"),
            std::format(
                R"(<a:xfrm><a:off x="{}" y="{}"/><a:ext cx="{}" cy="{}"/></a:xfrm><a:custGeom><a:avLst/><a:gdLst/><a:ahLst/>)"
                R"(<a:cxnLst/><a:rect l="0" t="0" r="r" b="b"/><a:pathLst><a:path w="{}" h="{}"{}>{}</a:path></a:pathLst></a:custGeom>{}{}{})",
                E(b.x0), E(b.y0), w, h, w, h, item.paint.fill.has_value() ? "" : R"( fill="none")", cmds, _Fill(item.paint),
                Line(item.paint.stroke, item.paint.opacity, _map), Effects(item.paint, _map)));
        return id;
    }

    int _Text(pugi::xml_node parent, const TextShape &t, const ad::Item &item, const std::string &name, Bounds *bounds)
    {
        if (t.text.empty())
        {
            return 0;
        }
        const double w  = _tm.Width(t.text, t.font) * 1.08 + 4;
        const double h  = t.font.size * 1.5;
        const Vec2   at = Apply(item.transform, t.pos);
        double       x  = at.x;
        if (t.align == HAlign::Center)
        {
            x -= w / 2;
        }
        else if (t.align == HAlign::Right)
        {
            x -= w;
        }
        const double cy = t.valign == VAlign::Middle ? at.y : at.y - t.font.size * 0.35;
        const Vec2   a  = _map.Map({x, cy - h / 2});
        const Vec2   b  = _map.Map({x + w, cy + h / 2});
        if (bounds != nullptr)
        {
            bounds->Add(a);
            bounds->Add(b);
        }
        const int         id    = _slide.NextId();
        auto              sp    = _Sp(parent, id, name, true);
        const char       *algn  = t.align == HAlign::Center ? "ctr" : (t.align == HAlign::Right ? "r" : "l");
        const Color       color = item.paint.fill.value_or(Color::Rgb(0xffffff));
        const std::string glow =
            t.halo.has_value()
                ? std::format(
                      R"(<a:effectLst><a:glow rad="{}"><a:srgbClr val="{}"><a:alpha val="70000"/></a:srgbClr></a:glow></a:effectLst>)",
                      E(_map.Len(t.halo->width / 2 + 1)), Hex(t.halo->color))
                : std::string();
        AppendXml(
            sp.child("p:spPr"),
            std::format(
                R"(<a:xfrm><a:off x="{}" y="{}"/><a:ext cx="{}" cy="{}"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:noFill/>)",
                E(a.x), E(a.y), std::max<int64_t>(1, E(b.x - a.x)), std::max<int64_t>(1, E(b.y - a.y))));
        auto body = AppendXml(
            sp,
            std::format(
                R"(<p:txBody><a:bodyPr wrap="none" lIns="0" tIns="0" rIns="0" bIns="0" anchor="ctr" rtlCol="0"><a:noAutofit/></a:bodyPr>)"
                R"(<a:lstStyle/><a:p><a:pPr algn="{}"/><a:r><a:rPr sz="{}" b="{}" dirty="0">{}{}{}</a:rPr><a:t/></a:r></a:p></p:txBody>)",
                algn, std::clamp<int64_t>(E(_map.Len(t.font.size) / kEmuPerPt * 100), 100, 400000), t.font.bold ? 1 : 0,
                SolidFill(color, item.paint.opacity), glow, t.font.family.empty() ? std::string() : R"(<a:latin typeface=""/>)"));
        auto rpr = body.child("a:p").child("a:r").child("a:rPr");
        if (auto latin = rpr.child("a:latin"); !latin.empty())
        {
            latin.attribute("typeface").set_value(t.font.family.c_str());
        }
        body.child("a:p").child("a:r").child("a:t").text().set(t.text.c_str());
        return id;
    }

    Slide              &_slide;
    const Mapping      &_map;
    const TextMeasurer &_tm;
};

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

SceneOptions ExportScene()
{
    SceneOptions o;
    o.editor_chrome = false;
    return o;
}

/// Items of a frame grouped by owner, in drawing order of the first item of each owner.
std::vector<std::pair<std::string, std::vector<const Item *>>> ByOwner(const Frame &f, std::string_view prefix = {})
{
    std::vector<std::pair<std::string, std::vector<const Item *>>> out;
    std::map<std::string, size_t>                                  index;
    for (const auto &it : f.items)
    {
        if (!prefix.empty() && !it.owner.starts_with(prefix))
        {
            continue;
        }
        auto [pos, inserted] = index.try_emplace(it.owner, out.size());
        if (inserted)
        {
            out.emplace_back(it.owner, std::vector<const Item *>{});
        }
        out[pos->second].second.push_back(&it);
    }
    return out;
}

Rect ItemsBounds(std::span<const Item *const> items, const TextMeasurer &tm)
{
    Bounds b;
    for (const Item *it : items)
    {
        std::visit(
            [&](const auto &s)
            {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, RectShape>)
                {
                    b.Add(Apply(it->transform, {s.rect.x, s.rect.y}));
                    b.Add(Apply(it->transform, {s.rect.x + s.rect.w, s.rect.y + s.rect.h}));
                }
                else if constexpr (std::is_same_v<T, EllipseShape>)
                {
                    b.Add(Apply(it->transform, {s.center.x - s.rx, s.center.y - s.ry}));
                    b.Add(Apply(it->transform, {s.center.x + s.rx, s.center.y + s.ry}));
                }
                else if constexpr (std::is_same_v<T, PathShape>)
                {
                    for (const auto &seg : s.path.Segments())
                    {
                        if (seg.kind != Path::Kind::Close)
                        {
                            b.Add(Apply(it->transform, seg.to));
                        }
                    }
                }
                else if constexpr (std::is_same_v<T, ArcShape>)
                {
                    b.Add({s.center.x - s.radius, s.center.y - s.radius});
                    b.Add({s.center.x + s.radius, s.center.y + s.radius});
                }
                else
                {
                    const double w = tm.Width(s.text, s.font);
                    b.Add(Apply(it->transform, s.pos) - Vec2{w / 2, s.font.size});
                    b.Add(Apply(it->transform, s.pos) + Vec2{w / 2, s.font.size / 2});
                }
            },
            it->shape);
    }
    return b.Valid() ? Rect{b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0} : Rect{};
}

std::string Label(const Model &m, const std::string &owner)
{
    // "node:svcA" -> "Node Service A"
    const auto colon = owner.find(':');
    if (colon == std::string::npos)
    {
        return owner;
    }
    const std::string kind = owner.substr(0, colon);
    const std::string id   = owner.substr(colon + 1);
    if (kind == "node")
    {
        if (const Node *n = m.FindNode(id); n != nullptr)
        {
            return "Node " + n->label;
        }
    }
    if (kind == "edge")
    {
        return "Edge " + id;
    }
    return "Step " + id;
}

std::vector<std::pair<double, double>> Segments(const Model &m, const VectorOptions &opt)
{
    if (!opt.segments.empty())
    {
        return opt.segments;
    }
    return {{0.0, std::max(1.0, m.scenario.duration)}};
}

// ---------------------------------------------------------------------------
// Animated slides
// ---------------------------------------------------------------------------

/// Effect id -> PowerPoint emphasis.
void Emphasis(Timeline &tl, int spid, const Step &s, const EffectDef *fx, double delay, double duration)
{
    const int         repeat = s.repeat > 0 ? s.repeat : (fx != nullptr ? fx->repeat : 1);
    const std::string id     = s.effect;
    if (id == "fade-in")
    {
        tl.Entrance(spid, delay, duration);
        return;
    }
    if (id == "fade-out")
    {
        tl.Exit(spid, delay, duration);
        return;
    }
    if (id == "shake" || id == "wobble")
    {
        tl.Rotate(spid, delay, duration, id == "shake" ? 4 : 8, true, std::max(2, repeat * 2));
        return;
    }
    if (id == "spin")
    {
        tl.Rotate(spid, delay, duration, 360, false, repeat);
        return;
    }
    if (id == "blink")
    {
        tl.Blink(spid, delay, duration, repeat);
        return;
    }
    // plugin effects: by their dominant property
    if (fx != nullptr && !fx->tracks.empty() && id != "pulse" && id != "glow" && id != "highlight" && id != "pop" && id != "bounce")
    {
        switch (fx->tracks.front().property)
        {
        case EffectProperty::Rotate:
            tl.Rotate(spid, delay, duration, 8, true, std::max(2, repeat * 2));
            return;
        case EffectProperty::Opacity:
            tl.Blink(spid, delay, duration, repeat);
            return;
        default:
            break;
        }
    }
    tl.Pulse(spid, delay, duration, 110, repeat);
}

void AnimatedSlide(Deck &deck, const Model &m, const Registry &reg, const TextMeasurer &tm, const Mapping &map, Color bg, double t0,
                   double t1)
{
    Slide &slide = deck.AddSlide();
    slide.SetBackground(bg);
    ShapeWriter        sw(slide, map, tm);
    const SceneOptions so = ExportScene();

    // the diagram at rest
    const Frame                base = BuildFrame(m, -1, so, tm, reg);
    std::map<std::string, int> spids;
    for (const auto &[owner, items] : ByOwner(base))
    {
        spids[owner] = sw.Group(slide.Tree(), items, Label(m, owner));
    }

    Timeline tl(slide);
    for (const Step &s : m.scenario.steps)
    {
        const double start = s.start;
        const double end   = s.start + std::max(1.0, s.duration);
        if (end <= t0 || start >= t1)
        {
            continue;
        }
        const double      from  = std::max(start, t0);
        const double      delay = from - t0;
        const double      dur   = end - from;
        const std::string owner = "step:" + s.id;

        switch (s.type)
        {
        case StepType::Message:
        {
            // every packet (and the label) follows its own sampled positions
            constexpr int                                               kSamples = 30;
            std::map<std::string, std::vector<std::pair<double, Vec2>>> tracks;
            std::map<std::string, Frame>                                first;
            for (int k = 0; k <= kSamples; ++k)
            {
                const double t = from + (end - from) * k / kSamples - (k == kSamples ? 0.5 : 0);
                const Frame  f = BuildFrame(m, t, so, tm, reg);
                for (const auto &[part, items] : ByOwner(f, owner + "/"))
                {
                    if (part.ends_with("/trail"))
                    {
                        continue;
                    }
                    tracks[part].emplace_back(t, ItemsBounds(items, tm).Center());
                    if (!first.contains(part))
                    {
                        Frame copy;
                        for (const Item *it : items)
                        {
                            copy.items.push_back(*it);
                        }
                        first[part] = std::move(copy);
                    }
                }
            }
            for (const auto &[part, track] : tracks)
            {
                std::vector<const Item *> items;
                for (const auto &it : first[part].items)
                {
                    items.push_back(&it);
                }
                const int id = sw.Group(slide.Tree(), items, "Message " + s.id);
                if (id == 0)
                {
                    continue;
                }
                const double appear = track.front().first - t0;
                const double gone   = track.back().first - t0;
                tl.Entrance(id, appear);
                if (track.size() > 1)
                {
                    std::string path = "M 0 0";
                    const Vec2  o    = track.front().second;
                    for (size_t i = 1; i < track.size(); ++i)
                    {
                        const Vec2 d = track[i].second - o;
                        path += std::format(" L {:.5f} {:.5f}", map.Len(d.x) / map.sw, map.Len(d.y) / map.sh);
                    }
                    path += " E";
                    tl.Motion(id, appear, std::max(1.0, gone - appear), path, static_cast<int>(track.size()));
                }
                tl.Exit(id, gone + 50);
            }
            break;
        }
        case StepType::Timer:
        {
            // flip book: the countdown changes twice per second
            const double frame_ms = 500;
            for (double t = from; t < end; t += frame_ms)
            {
                const Frame               f     = BuildFrame(m, std::min(t + 1, end - 1), so, tm, reg);
                const auto                parts = ByOwner(f, owner);
                std::vector<const Item *> items;
                for (const auto &[part, its] : parts)
                {
                    items.insert(items.end(), its.begin(), its.end());
                }
                const int id = sw.Group(slide.Tree(), items, "Timer " + s.id);
                if (id != 0)
                {
                    tl.Entrance(id, t - t0);
                    tl.Exit(id, std::min(t + frame_ms, end) - t0);
                }
            }
            break;
        }
        case StepType::Note:
        case StepType::Action:
        case StepType::Link:
        {
            const Frame               f = BuildFrame(m, from + std::min(dur / 2, 400.0), so, tm, reg);
            std::vector<const Item *> items;
            for (const auto &[part, its] : ByOwner(f, owner))
            {
                items.insert(items.end(), its.begin(), its.end());
            }
            const int id = sw.Group(slide.Tree(), items, StepTypeLabel(s.type) + " " + s.id);
            if (id != 0)
            {
                tl.Entrance(id, delay, 250);
                tl.Exit(id, end - t0, 250);
            }
            break;
        }
        case StepType::State:
        {
            // the node in that state, over the node at rest
            const Frame f     = BuildFrame(m, from + dur / 2, so, tm, reg);
            const auto  parts = ByOwner(f, "node:" + s.node_id);
            if (!parts.empty())
            {
                const int id = sw.Group(slide.Tree(), parts.front().second, "State " + s.id);
                if (id != 0)
                {
                    tl.Entrance(id, delay, 200);
                    tl.Exit(id, end - t0, 200);
                }
            }
            break;
        }
        case StepType::Effect:
        {
            if (const auto it = spids.find("node:" + s.node_id); it != spids.end() && it->second != 0)
            {
                Emphasis(tl, it->second, s, reg.FindEffect(s.effect, &m.library), delay, dur);
            }
            break;
        }
        }
    }
    if (tl.Effects() == 0)
    {
        slide.Root().remove_child("p:timing");
    }
}

// ---------------------------------------------------------------------------
// Morph slides
// ---------------------------------------------------------------------------

void MorphSlide(Deck &deck, const Model &m, const Registry &reg, const TextMeasurer &tm, const Mapping &map, Color bg, double t,
                std::optional<double> morph_ms, std::optional<double> advance_ms)
{
    Slide &slide = deck.AddSlide();
    slide.SetBackground(bg);
    ShapeWriter sw(slide, map, tm);
    const Frame f = BuildFrame(m, t, ExportScene(), tm, reg);
    for (const auto &[owner, items] : ByOwner(f))
    {
        if (owner.ends_with("/trail"))
        {
            continue; // trails change shape every frame: morphing them looks noisy
        }
        int k = 0;
        for (const Item *it : items)
        {
            // "!!" names pair the shapes of consecutive slides for the Morph transition
            sw.Item(slide.Tree(), *it, std::format("!!{}#{}", owner, k++), nullptr);
        }
    }
    if (morph_ms.has_value())
    {
        slide.SetMorphTransition(*morph_ms, advance_ms);
    }
}

} // namespace

void WriteVectorSlides(const Target &target, const Model &m, const Registry &reg, const TextMeasurer &tm, const VectorOptions &opt)
{
    Deck        deck(target);
    const Rect  content = ContentBounds(m, tm, reg).Adjusted(24);
    const auto  map     = MakeMapping(content, deck.Size(), opt.margin);
    const Color bg      = Color::Parse(m.scene.background, palette::kCanvasBg);
    const auto  segs    = Segments(m, opt);

    if (opt.mode == VectorOptions::Mode::Animated)
    {
        for (const auto &[t0, t1] : segs)
        {
            AnimatedSlide(deck, m, reg, tm, map, bg, t0, t1);
        }
    }
    else
    {
        const double step  = std::max(50.0, opt.morph_step_ms);
        bool         first = true;
        double       prev  = 0;
        for (size_t si = 0; si < segs.size(); ++si)
        {
            const auto [t0, t1] = segs[si];
            const int n         = std::max(1, static_cast<int>(std::ceil((t1 - t0) / step)));
            for (int k = (si == 0 ? 0 : 1); k <= n; ++k)
            {
                const double t       = std::min(t1, t0 + k * step);
                const bool   seg_end = k == n;
                const auto   morph   = first ? std::nullopt : std::optional<double>(t - prev);
                const auto   advance = seg_end ? std::nullopt : std::optional<double>(0.0); // stop at segment ends: click
                MorphSlide(deck, m, reg, tm, map, bg, std::min(t, t1 - 1), morph, advance);
                first = false;
                prev  = t;
            }
        }
    }
    deck.Finish();
}

} // namespace ad::pptx
