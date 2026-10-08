#include "Scene.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <numbers>

#include "Engine/Effects.hpp"
#include "Engine/Engine.hpp"
#include "Engine/Shapes.hpp"

namespace ad
{

size_t Utf8Length(std::string_view s)
{
    return static_cast<size_t>(std::ranges::count_if(s,
                                                     [](char c)
                                                     {
                                                         return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U;
                                                     }));
}

double ApproxTextMeasurer::Width(std::string_view utf8, const Font &font) const
{
    return 0.6 * font.size * (font.bold ? 1.05 : 1.0) * static_cast<double>(Utf8Length(utf8));
}

namespace
{

constexpr Color kWhite      = Color::Rgb(0xffffff);
constexpr Color kArrowColor = Color::Rgb(0x8aa0c0);
constexpr Color kDark       = palette::kPanelBg;
constexpr Color kCyan       = Color::Rgb(0x22d3ee);
constexpr Color kSubtitle   = Color::Rgb(0xb7c6e4);

/// Highlight ring around a node caused by messages and actions.
struct Highlight
{
    Color  ring;
    double opacity = 0.9;
};

Stroke Solid(Color c, double w) { return Stroke{c, w, {}, 0, false}; }

Paint FillOnly(Color c, double opacity = 1, PaintEffect fx = PaintEffect::None) { return Paint{c, std::nullopt, opacity, fx}; }

Paint StrokeOnly(Stroke s, double opacity = 1, PaintEffect fx = PaintEffect::None)
{
    return Paint{std::nullopt, std::move(s), opacity, fx};
}

std::vector<double> DashFor(std::string_view stroke_style, double width)
{
    if (stroke_style == "dashed")
    {
        return {7, 6};
    }
    if (stroke_style == "dotted")
    {
        return {std::max(1.0, width * 0.6), width * 2.2};
    }
    return {};
}

class FrameBuilder
{
public:
    FrameBuilder(const Model &m, double t, const SceneOptions &o, const TextMeasurer &tm, const Registry &reg)
        : _m(m), _t(t), _o(o), _tm(tm), _reg(reg)
    {
    }

    Frame Build()
    {
        const auto active = ActiveSteps(_m, _t);
        _ComputeNodeEffects(active);
        for (const auto &e : _m.edges)
        {
            _DrawEdge(e);
        }
        for (const auto &n : _m.nodes)
        {
            _DrawNode(n);
        }
        for (const Step *s : active)
        {
            const double p = std::clamp((_t - s->start) / std::max(1.0, s->duration), 0.0, 1.0);
            switch (s->type)
            {
            case StepType::Message:
                _DrawMessage(*s, p);
                break;
            case StepType::Timer:
                _DrawTimer(*s, p);
                break;
            case StepType::Note:
                _DrawNote(*s, p);
                break;
            case StepType::Action:
                _DrawAction(*s);
                break;
            case StepType::Link:
                _DrawLink(*s);
                break;
            case StepType::State:
            case StepType::Effect:
                break; // shown on the nodes themselves
            }
        }
        return std::move(_frame);
    }

private:
    void _Add(Shape shape, Paint paint, std::optional<Transform> tr = std::nullopt, std::optional<Path> clip = std::nullopt)
    {
        _frame.items.push_back(Item{std::move(shape), std::move(paint), std::move(tr), std::move(clip)});
    }

    void _Text(Vec2 pos, std::string s, Font font, Color color, HAlign a = HAlign::Left, VAlign v = VAlign::Baseline, double opacity = 1,
               std::optional<Stroke> halo = std::nullopt, std::optional<Transform> tr = std::nullopt)
    {
        _Add(TextShape{pos, std::move(s), font, a, v, std::move(halo)}, FillOnly(color, opacity), std::move(tr));
    }

    // -----------------------------------------------------------------------
    // Effects and highlights (computed before nodes are drawn)
    // -----------------------------------------------------------------------
    void _ComputeNodeEffects(const std::vector<const Step *> &active)
    {
        auto soft = [this](const std::string &node_id, Color c)
        {
            if (!node_id.empty() && !_hl.contains(node_id))
            {
                _hl[node_id] = Highlight{c, 0.7};
            }
        };
        for (const Step *s : active)
        {
            const double p = std::clamp((_t - s->start) / std::max(1.0, s->duration), 0.0, 1.0);
            switch (s->type)
            {
            case StepType::Effect:
                if (const EffectDef *def = _reg.FindEffect(s->effect, &_m.library); def != nullptr)
                {
                    const auto override_color = Color::Parse(s->color);
                    const auto st =
                        EvaluateEffect(*def, p, s->repeat, s->intensity, override_color.has_value() ? &*override_color : nullptr);
                    _fx[s->node_id].Combine(st);
                }
                break;
            case StepType::Message:
            {
                const Color c = Color::Parse(s->color, MsgVariant(s->variant).color);
                if (p < 0.2)
                {
                    soft(s->from, c);
                }
                if (p > 0.8)
                {
                    soft(s->to, c);
                }
                break;
            }
            case StepType::Action:
                soft(s->node_id, Color::Parse(s->color, Color::Rgb(0x38bdf8)));
                break;
            default:
                break;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Edges
    // -----------------------------------------------------------------------
    void _ArrowHead(std::string_view head, Vec2 base, Vec2 dir, Color c, double opacity)
    {
        const Vec2 perp{-dir.y, dir.x};
        const Vec2 tip = base + dir * kArrowLen;
        if (head == "open")
        {
            Path p;
            p.MoveTo(base + perp * 5);
            p.LineTo(tip);
            p.LineTo(base - perp * 5);
            Stroke s    = Solid(c, 2);
            s.round_cap = true;
            _Add(PathShape{std::move(p)}, StrokeOnly(s, opacity));
            return;
        }
        if (head == "diamond")
        {
            const Vec2 mid = base + dir * (kArrowLen / 2);
            const Vec2 pts[4]{base, mid + perp * 5, tip, mid - perp * 5};
            _Add(PathShape{Path::Polygon(pts)}, FillOnly(c, opacity));
            return;
        }
        if (head == "circle")
        {
            _Add(EllipseShape{base + dir * (kArrowLen / 2), 5, 5}, FillOnly(c, opacity));
            return;
        }
        const Vec2 pts[3]{base + perp * 5, tip, base - perp * 5};
        _Add(PathShape{Path::Polygon(pts)}, FillOnly(c, opacity));
    }

    void _DrawEdge(const Edge &e)
    {
        const auto g = ComputeEdgeGeometry(_m, e, _reg);
        if (!g.has_value())
        {
            return;
        }
        const bool   selected = _o.editor_chrome && _o.selection.Is(Selection::Kind::Edge, e.id);
        const Color  base     = Color::Parse(e.style.color.value_or(""), Color::Parse(_m.scene.edge_color, Color::Rgb(0x5f7196)));
        const double width    = e.style.width.value_or(2.2);
        Stroke       st       = Solid(selected ? kWhite : base, selected ? width + 0.8 : width);
        st.dash               = DashFor(e.style.stroke_style.value_or("solid"), width);
        st.round_cap          = e.style.stroke_style.value_or("") == "dotted";
        _Add(PathShape{g->path}, StrokeOnly(st));

        const Color arrow = e.style.color.has_value() ? base : kArrowColor;
        if (g->arrow_end)
        {
            _ArrowHead(e.style.arrow_end.value_or("triangle"), g->end, g->path.EndDirection(), arrow, 1);
        }
        if (g->arrow_start)
        {
            _ArrowHead(e.style.arrow_start.value_or("triangle"), g->start, g->path.StartDirection() * -1.0, arrow, 1);
        }

        if (!e.label.empty())
        {
            const FlatPath fp(g->path);
            const Vec2     pt    = fp.PointAlong(e.label_pos.value_or(0.5), e.label_off.value_or(10));
            const Color    color = Color::Parse(e.style.label_color.value_or(""), palette::kEdgeLabel);
            _Text(pt, e.label, Font{e.label_size.value_or(12), false}, color, HAlign::Center, VAlign::Middle, 1,
                  Solid(Color::Parse(_m.scene.background, palette::kCanvasBg), 3));
        }
        if (selected)
        {
            for (const Vec2 wp : e.waypoints)
            {
                _Add(EllipseShape{wp, 6, 6}, Paint{kDark, Solid(kWhite, 2), 1, PaintEffect::None});
            }
        }
    }

    // -----------------------------------------------------------------------
    // Nodes
    // -----------------------------------------------------------------------
    void _DrawNode(const Node &n)
    {
        const ElementType  &type        = _reg.Element(n.type, &_m.library);
        const NodeStyle     style       = type.style.Merged(n.style);
        const std::string   shape       = ResolveShape(style);
        const double        rad         = style.corner_radius.value_or(DefaultCornerRadius(shape));
        const std::string   custom      = style.custom_path.value_or("");
        const ResolvedState st          = ResolveNodeState(StateAt(_m, n.id, _t), style);
        const auto          fx_it       = _fx.find(n.id);
        const EffectState   fx          = fx_it != _fx.end() ? fx_it->second : EffectState{};
        const auto          hl_it       = _hl.find(n.id);
        const bool          selected    = _o.editor_chrome && _o.selection.Is(Selection::Kind::Node, n.id);
        const bool          connect_src = !_o.connect_from_node.empty() && _o.connect_from_node == n.id;
        const double        opacity     = std::clamp(style.opacity.value_or(1.0), 0.0, 1.0) * fx.opacity;

        std::optional<Transform> tr;
        if (fx.scale != 1 || fx.rotate != 0 || fx.dx != 0 || fx.dy != 0)
        {
            tr = Transform{n.Center(), fx.rotate, fx.scale, {fx.dx, fx.dy}};
        }

        const Rect r    = n.Bounds();
        const Path body = ShapeOutline(shape, r, rad, custom);
        Color      fill = st.fill;
        if (fx.tint > 0)
        {
            fill = fill.Mix(fx.color, fx.tint);
        }
        const double border_w = style.stroke_width.value_or(2.0);
        Stroke       border   = Solid(connect_src ? kCyan
                                      : selected  ? kWhite
                                                  : st.ring,
                              selected || connect_src ? std::max(3.0, border_w) : border_w);
        border.dash       = DashFor(style.stroke_style.value_or("solid"), border_w);
        const bool shadow = style.shadow.value_or(true);
        _Add(PathShape{body}, Paint{fill, border, opacity, shadow ? PaintEffect::Shadow : PaintEffect::None}, tr);

        const bool centered = IsCenteredShape(shape);
        if (!centered)
        {
            const Color accent = Color::Parse(n.accent, Color::Parse(type.accent, Color::Rgb(0x4f8cff)));
            _Add(RectShape{{n.x, n.y, 6, n.h}, 0}, FillOnly(accent, 0.9 * opacity), tr, body);
        }
        if (const Path deco = ShapeDecoration(shape, r); !deco.Empty())
        {
            _Add(PathShape{deco}, StrokeOnly(Solid(st.ring, 1.5), 0.6 * opacity), tr);
        }

        const Color  text_color = Color::Parse(style.text_color.value_or(""), Color::Parse(_m.scene.text_color, Color::Rgb(0xf2f6ff)));
        const double title_fs   = style.font_size.value_or(15);
        // subtitle priority: step label > node subtitle (in the default state) > state label
        const std::string &sub = (st.id == "ok" && !st.custom_label && !n.subtitle.empty()) ? n.subtitle : st.label;
        const Font         sub_font{st.label_size.value_or(11), false};
        if (centered)
        {
            const double cy = n.y + n.h / 2;
            _Text({n.x + n.w / 2, cy - 1}, n.label, Font{title_fs, true}, text_color, HAlign::Center, VAlign::Baseline, opacity,
                  std::nullopt, tr);
            _Text({n.x + n.w / 2, cy + 15}, sub, sub_font, kSubtitle, HAlign::Center, VAlign::Baseline, 0.85 * opacity, std::nullopt, tr);
        }
        else
        {
            const bool   icon   = style.show_icon.value_or(true) && !type.icon.empty();
            const double text_x = n.x + (icon ? 40 : 16);
            if (icon)
            {
                _Text({n.x + 18, n.y + n.h / 2 + 6}, type.icon, Font{16, false}, Color::Rgb(0xe6eefc), HAlign::Left, VAlign::Baseline,
                      opacity, std::nullopt, tr);
            }
            _Text({text_x, n.y + n.h / 2 - 4}, n.label, Font{title_fs, true}, text_color, HAlign::Left, VAlign::Baseline, opacity,
                  std::nullopt, tr);
            _Text({text_x, n.y + n.h / 2 + 14}, sub, sub_font, kSubtitle, HAlign::Left, VAlign::Baseline, 0.85 * opacity, std::nullopt, tr);
        }

        for (const auto &port : n.ports)
        {
            const bool   big  = selected || _o.connect_mode;
            const Color  pf   = _o.connect_mode || selected ? kCyan : kDark;
            const Color  ring = _o.connect_mode ? Color::Rgb(0xa5f3fc) : kCyan;
            const double pr   = big ? 6.0 : 4.0;
            _Add(EllipseShape{{n.x + port.dx, n.y + port.dy}, pr, pr}, Paint{pf, Solid(ring, 2), 1, PaintEffect::None}, tr);
        }

        double ring_opacity = 0;
        Color  ring_color   = fx.color;
        if (hl_it != _hl.end())
        {
            ring_opacity = hl_it->second.opacity;
            ring_color   = hl_it->second.ring;
        }
        if (fx.glow * 0.95 > ring_opacity)
        {
            ring_opacity = fx.glow * 0.95;
            ring_color   = fx.color;
        }
        if (ring_opacity > 0)
        {
            const Path ring = ShapeOutline(shape, r.Adjusted(6), rad + 6, custom);
            _Add(PathShape{ring}, StrokeOnly(Solid(ring_color, 3), ring_opacity, PaintEffect::Glow), tr);
        }
    }

    // -----------------------------------------------------------------------
    // Message: packets travelling along the edge
    // -----------------------------------------------------------------------
    void _Packet(std::string_view shape, Vec2 pos, double angle, double size, Color color)
    {
        const Transform tr{{0, 0}, angle, std::clamp(size, 0.2, 5.0), pos};
        if (shape == "dot")
        {
            _Add(EllipseShape{{0, 0}, 7, 7}, FillOnly(color, 1, PaintEffect::Glow), tr);
            return;
        }
        if (shape == "square")
        {
            _Add(RectShape{{-8, -8, 16, 16}, 2}, FillOnly(color, 1, PaintEffect::Glow), tr);
            return;
        }
        if (shape == "diamond")
        {
            const Vec2 pts[4]{{0, -9}, {9, 0}, {0, 9}, {-9, 0}};
            _Add(PathShape{Path::Polygon(pts)}, FillOnly(color, 1, PaintEffect::Glow), tr);
            return;
        }
        if (shape == "envelope")
        {
            _Add(RectShape{{-12, -8, 24, 16}, 2}, FillOnly(color, 1, PaintEffect::Glow), tr);
            Path flap;
            flap.MoveTo({-12, -8});
            flap.LineTo({0, 1});
            flap.LineTo({12, -8});
            _Add(PathShape{std::move(flap)}, StrokeOnly(Solid(kDark, 1.5), 0.85), tr);
            return;
        }
        if (shape == "arrow")
        {
            const Vec2 pts[4]{{-10, -7}, {10, 0}, {-10, 7}, {-5, 0}};
            _Add(PathShape{Path::Polygon(pts)}, FillOnly(color, 1, PaintEffect::Glow), tr);
            return;
        }
        _Add(RectShape{{-13, -8, 26, 16}, 8}, FillOnly(color, 1, PaintEffect::Glow), tr);
        const Vec2 tri[3]{{2, -4}, {8, 0}, {2, 4}};
        _Add(PathShape{Path::Polygon(tri)}, FillOnly(kDark, 0.85), tr);
    }

    void _DrawMessage(const Step &s, double p)
    {
        const Node *a = _m.FindNode(s.from);
        const Node *b = _m.FindNode(s.to);
        if (a == nullptr || b == nullptr)
        {
            return;
        }
        const auto &variant = MsgVariant(s.variant);
        const Color color   = Color::Parse(s.color, variant.color);

        const Edge *edge    = !s.edge_id.empty() ? _m.FindEdge(s.edge_id) : _m.EdgeBetween(s.from, s.to);
        const auto  g       = edge != nullptr ? ComputeEdgeGeometry(_m, *edge, _reg) : std::nullopt;
        bool        forward = true;
        Path        path;
        if (g.has_value())
        {
            path    = g->path;
            forward = edge->from == s.from;
        }
        else
        {
            // no edge -- a straight line between the node borders
            const Vec2 start = geom::BorderPoint(a->Center(), a->w / 2 + 3, a->h / 2 + 3, b->Center());
            const Vec2 end   = geom::BorderPoint(b->Center(), b->w / 2 + 8, b->h / 2 + 8, a->Center());
            path             = Path::Line(start, end);
        }
        const FlatPath fp(path);

        if (s.trail)
        {
            Stroke tail = Solid(color, 3);
            if (variant.dash > 0)
            {
                tail.dash = {variant.dash, variant.gap};
            }
            _Add(PathShape{path}, StrokeOnly(tail, 0.25));
        }

        // a stream of packets: all of them fit into the step, the last one arrives at p = 1
        const int    count   = std::clamp(s.packet_count, 1, 10);
        const double spacing = 0.14;
        const double span    = 1 + (count - 1) * spacing;
        Vec2         lead_pos;
        bool         lead = false;
        for (int i = 0; i < count; ++i)
        {
            const double pi = p * span - i * spacing;
            if (pi < 0 || pi > 1)
            {
                continue;
            }
            const double e   = ApplyEasing(s.easing, pi);
            const double len = (forward ? e : 1 - e) * fp.Length();
            const Vec2   pos = fp.PointAtLength(len);
            const Vec2   dir = fp.DirectionAt(len, forward);
            _Packet(s.packet, pos, std::atan2(dir.y, dir.x) * 180 / std::numbers::pi, s.packet_size, color);
            if (!lead)
            {
                lead_pos = pos;
                lead     = true;
            }
        }

        if (lead && !s.label.empty())
        {
            const Font   font{11, true};
            const double tw = _tm.Width(s.label, font) + 14;
            const Vec2   o{lead_pos.x, lead_pos.y - 16 - (s.packet_size - 1) * 8};
            _Add(RectShape{{o.x - tw / 2, o.y - 13, tw, 18}, 5}, Paint{kDark, Solid(color, 1), 0.82, PaintEffect::None});
            _Text(o, s.label, font, color, HAlign::Center);
        }
    }

    // -----------------------------------------------------------------------
    // Timer: countdown ring above the node
    // -----------------------------------------------------------------------
    void _DrawTimer(const Step &s, double p)
    {
        const Node *n = _m.FindNode(s.node_id);
        if (n == nullptr)
        {
            return;
        }
        const double     total     = s.seconds > 0 ? s.seconds : std::round(s.duration / 1000);
        const double     remaining = std::max(0.0, total * (1 - p));
        const Vec2       c{n->x + n->w - 6, n->y - 6};
        constexpr double kR     = 20;
        const bool       danger = remaining <= 1.5;
        _Add(EllipseShape{c, kR + 4, kR + 4}, FillOnly(kDark, 0.9));
        _Add(EllipseShape{c, kR, kR}, StrokeOnly(Solid(Color::Rgb(0x334155), 4)));
        if (p < 1)
        {
            Stroke arc    = Solid(Color::Parse(s.color, danger ? Color::Rgb(0xef4444) : Color::Rgb(0xf59e0b)), 4);
            arc.round_cap = true;
            _Add(ArcShape{c, kR, -90, 360 * (1 - p)}, StrokeOnly(arc));
        }
        const std::string label = std::format("{}{}", static_cast<int64_t>(std::ceil(remaining)), TimeUnit(s.unit).short_label);
        const auto        len   = Utf8Length(label);
        const double      fs    = len >= 5 ? 10 : len == 4 ? 12 : 15; // longer labels get smaller
        _Text({c.x, c.y + 5}, label, Font{fs, true}, danger ? Color::Rgb(0xfca5a5) : Color::Rgb(0xfcd34d), HAlign::Center);
        if (!s.label.empty())
        {
            _Text({c.x, c.y + kR + 15}, s.label, Font{10, false}, Color::Rgb(0xcbd5e1), HAlign::Center);
        }
    }

    // -----------------------------------------------------------------------
    // Note: a card fading in and out
    // -----------------------------------------------------------------------
    void _DrawNote(const Step &s, double p)
    {
        const double       fade = p < 0.12 ? p / 0.12 : p > 0.88 ? (1 - p) / 0.12 : 1;
        const std::string &txt  = s.text.empty() ? kNoteDefault : s.text;
        const Font         font{12.5, false};
        const double       w     = _tm.Width(txt, font) + 28;
        const Color        color = Color::Parse(s.color, Color::Rgb(0xfbbf24));
        const Rect         r{s.x, s.y, w, 30};
        const Path         card = Path::RoundedRect(r.x, r.y, r.w, r.h, 8);
        _Add(RectShape{r, 8}, Paint{Color::Rgb(0x111a2e), Solid(color, 1.5), fade, PaintEffect::Shadow});
        _Add(RectShape{{s.x, s.y, 5, 30}, 0}, FillOnly(color, fade), std::nullopt, card);
        _Text({s.x + 14, s.y + 19}, txt, font, Color::Rgb(0xe6eefc), HAlign::Left, VAlign::Baseline, fade);
    }

    // -----------------------------------------------------------------------
    // Action: badge with a spinner under the node
    // -----------------------------------------------------------------------
    void _DrawAction(const Step &s)
    {
        const Node *n = _m.FindNode(s.node_id);
        if (n == nullptr)
        {
            return;
        }
        const Color        color = Color::Parse(s.color, Color::Rgb(0x38bdf8));
        const std::string &txt   = s.text.empty() ? kActionDefault : s.text;
        const Font         font{12, true};
        const double       tw   = _tm.Width(txt, font);
        constexpr double   kPad = 12, kSr = 7, kGapX = 7;
        const double       w = kPad + kSr * 2 + kGapX + tw + kPad;
        const Vec2         c{n->x + n->w / 2, n->y + n->h + 18};
        const double       left = c.x - w / 2;
        _Add(RectShape{{left, c.y - 13, w, 26}, 13}, Paint{kDark, Solid(color, 1.2), 0.92, PaintEffect::Shadow});
        const Vec2 sc{left + kPad + kSr, c.y};
        _Add(EllipseShape{sc, kSr, kSr}, StrokeOnly(Solid(color, 2.4), 0.25));
        Stroke spin    = Solid(color, 2.4);
        spin.round_cap = true;
        _Add(ArcShape{sc, kSr, std::fmod(_t / 1000 * 300, 360), 360 * 0.65}, StrokeOnly(spin));
        _Text({sc.x + kSr + kGapX, c.y + 4}, txt, font, color);
    }

    // -----------------------------------------------------------------------
    // Link: animated line over the edge + badge
    // -----------------------------------------------------------------------
    void _DrawLink(const Step &s)
    {
        const Edge *e = _m.FindEdge(s.edge_id);
        const auto  g = e != nullptr ? ComputeEdgeGeometry(_m, *e, _reg) : std::nullopt;
        if (!g.has_value())
        {
            return;
        }
        const Color color = Color::Parse(s.color, Color::Rgb(0xf87171));
        const auto &anim  = LinkAnim(s.anim);
        Stroke      st    = Solid(color, 4);
        st.round_cap      = true;
        double op         = 0.9;
        if (anim.dashed)
        {
            st.dash = {7, 6};
        }
        if (anim.flow)
        {
            st.dash_offset = std::fmod(-_t / 1000 * 26, 1000);
        }
        if (anim.pulse)
        {
            const double k = 0.5 + 0.5 * std::sin(_t / 1000 * std::numbers::pi * 2);
            st.width       = 3 + 2.5 * k;
            op             = 0.45 + 0.45 * k;
        }
        _Add(PathShape{g->path}, StrokeOnly(st, op));

        if (s.text.empty())
        {
            return;
        }
        const double   fs = s.label_size.value_or(12);
        const FlatPath fp(g->path);
        const Vec2     pt = fp.PointAlong(s.label_pos.value_or(0.5), s.label_off.value_or(22));
        const Font     font{fs, true};
        const double   tw = _tm.Width(s.text, font);
        const double   h  = std::max(24.0, fs + 12);
        const double   w  = tw + std::max(12.0, fs) * 2;
        _Add(RectShape{{pt.x - w / 2, pt.y - h / 2, w, h}, h / 2}, Paint{kDark, Solid(color, 1.2), 0.95, PaintEffect::Shadow});
        _Text(pt, s.text, font, color, HAlign::Center, VAlign::Middle);
    }

    inline static const std::string kNoteDefault   = "заметка";
    inline static const std::string kActionDefault = "Действие";

    const Model                                    &_m;
    double                                          _t;
    const SceneOptions                             &_o;
    const TextMeasurer                             &_tm;
    const Registry                                 &_reg;
    std::map<std::string, EffectState, std::less<>> _fx;
    std::map<std::string, Highlight, std::less<>>   _hl;
    Frame                                           _frame;
};

} // namespace

Frame BuildFrame(const Model &m, double t, const SceneOptions &opt, const TextMeasurer &tm, const Registry &reg)
{
    return FrameBuilder(m, t, opt, tm, reg).Build();
}

Rect ContentBounds(const Model &m, const TextMeasurer &tm, const Registry &reg)
{
    Bounds b;
    for (const auto &n : m.nodes)
    {
        b.Add(n.Bounds());
    }
    for (const auto &e : m.edges)
    {
        for (const Vec2 wp : e.waypoints)
        {
            b.Add(wp);
        }
    }
    for (const auto &s : m.scenario.steps)
    {
        if (s.type == StepType::Note)
        {
            const double w = tm.Width(s.text.empty() ? "заметка" : s.text, Font{12.5, false}) + 28;
            b.Add(Rect{s.x, s.y, w, 30});
        }
        else if (s.type == StepType::Action)
        {
            if (const Node *n = m.FindNode(s.node_id); n != nullptr)
            {
                const double w = tm.Width(s.text, Font{12, true}) + 50;
                b.Add(Rect{n->Center().x - w / 2, n->y + n->h + 5, w, 26});
            }
        }
        else if (s.type == StepType::Link && !s.text.empty())
        {
            const Edge *e = m.FindEdge(s.edge_id);
            const auto  g = e != nullptr ? ComputeEdgeGeometry(m, *e, reg) : std::nullopt;
            if (g.has_value())
            {
                const Vec2   pt = FlatPath(g->path).PointAlong(s.label_pos.value_or(0.5), s.label_off.value_or(22));
                const double fs = s.label_size.value_or(12);
                const double w  = tm.Width(s.text, Font{fs, true}) + std::max(12.0, fs) * 2;
                const double h  = std::max(24.0, fs + 12);
                b.Add(Rect{pt.x - w / 2, pt.y - h / 2, w, h});
            }
        }
    }
    return b.Empty() ? Rect{0, 0, 640, 360} : b.ToRect();
}

} // namespace ad
