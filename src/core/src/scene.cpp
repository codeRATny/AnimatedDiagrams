#include "ad/scene.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <numbers>

#include "ad/engine.hpp"

namespace ad {

std::size_t utf8Length(std::string_view s) {
    return static_cast<std::size_t>(
        std::ranges::count_if(s, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

double ApproxTextMeasurer::width(std::string_view utf8, const Font& font) const {
    return 0.6 * font.size * (font.bold ? 1.05 : 1.0) * static_cast<double>(utf8Length(utf8));
}

namespace {

constexpr Color kWhite = Color::rgb(0xffffff);
constexpr Color kEdgeColor = Color::rgb(0x5f7196);
constexpr Color kArrowColor = Color::rgb(0x8aa0c0);
constexpr Color kEdgeLabel = Color::rgb(0x9fb3d6);
constexpr Color kDark = Color::rgb(0x0b1220);
constexpr Color kCyan = Color::rgb(0x22d3ee);

struct Highlight {
    Color ring;
    double opacity = 0.9;
    double scale = 1;
};
using Highlights = std::map<std::string, Highlight, std::less<>>;

Stroke solid(Color c, double w) { return Stroke{c, w, {}, 0, false}; }

Paint fillOnly(Color c, double opacity = 1, Effect fx = Effect::None) { return Paint{c, std::nullopt, opacity, fx}; }

Paint strokeOnly(Stroke s, double opacity = 1, Effect fx = Effect::None) {
    return Paint{std::nullopt, std::move(s), opacity, fx};
}

class Builder {
public:
    Builder(const Model& m, double t, const SceneOptions& o, const TextMeasurer& tm) : m_(m), t_(t), o_(o), tm_(tm) {}

    Frame build() {
        const auto active = activeSteps(m_, t_);
        computeHighlights(active);
        for (const auto& e : m_.edges) drawEdge(e);
        for (const auto& n : m_.nodes) drawNode(n);
        for (const Step* s : active) {
            const double p = std::clamp((t_ - s->start) / std::max(1.0, s->duration), 0.0, 1.0);
            switch (s->type) {
                case StepType::Message: drawMessage(*s, p); break;
                case StepType::Timer: drawTimer(*s, p); break;
                case StepType::Note: drawNote(*s, p); break;
                case StepType::Action: drawAction(*s); break;
                case StepType::Link: drawLink(*s); break;
                case StepType::State:
                case StepType::Pulse: break;  // отражаются на самих узлах
            }
        }
        return std::move(f_);
    }

private:
    void add(Shape shape, Paint paint, std::optional<Transform> tr = std::nullopt, std::optional<Clip> clip = std::nullopt) {
        f_.items.push_back(Item{std::move(shape), std::move(paint), tr, clip});
    }

    void text(Vec2 pos, std::string s, Font font, Color color, HAlign a = HAlign::Left, VAlign v = VAlign::Baseline,
              double opacity = 1, std::optional<Stroke> halo = std::nullopt, std::optional<Transform> tr = std::nullopt) {
        add(TextShape{pos, std::move(s), font, a, v, std::move(halo)}, fillOnly(color, opacity), tr);
    }

    // ---- подсветки: считаются до отрисовки узлов -----------------------------
    void computeHighlights(const std::vector<const Step*>& active) {
        auto soft = [this](const std::string& nodeId, Color c) {
            if (nodeId.empty()) return;
            const auto it = hl_.find(nodeId);
            if (it != hl_.end() && it->second.scale != 1) return;  // пульс важнее
            hl_[nodeId] = Highlight{c, 0.7, 1};
        };
        for (const Step* s : active) {
            const double p = (t_ - s->start) / std::max(1.0, s->duration);
            switch (s->type) {
                case StepType::Pulse: {
                    const double k = 0.5 + 0.5 * std::sin(p * std::numbers::pi * 3);
                    hl_[s->nodeId] = Highlight{Color::parseOr(s->color, kCyan), 0.35 + 0.55 * k, 1 + 0.05 * k};
                    break;
                }
                case StepType::Message: {
                    const Color c = msgVariant(s->variant).color;
                    if (p < 0.2) soft(s->from, c);
                    if (p > 0.8) soft(s->to, c);
                    break;
                }
                case StepType::Action: soft(s->nodeId, Color::parseOr(s->color, Color::rgb(0x38bdf8))); break;
                default: break;
            }
        }
    }

    // ---- связи --------------------------------------------------------------
    void arrowHead(Vec2 base, Vec2 dir, Color c) {
        const Vec2 perp{-dir.y, dir.x};
        Path p;
        p.moveTo(base + perp * 5);
        p.lineTo(base + dir * kArrowLen);
        p.lineTo(base - perp * 5);
        p.lineTo(base + perp * 5);
        add(PathShape{std::move(p)}, fillOnly(c));
    }

    void drawEdge(const Edge& e) {
        const auto g = edgeGeometry(m_, e);
        if (!g) return;
        const bool selected = o_.editorChrome && o_.selection.is(Selection::Kind::Edge, e.id);
        Stroke st = solid(selected ? kWhite : kEdgeColor, selected ? 3 : 2.2);
        if (e.style == "dashed") st.dash = {7, 6};
        add(PathShape{g->path}, strokeOnly(st));
        arrowHead(g->end, g->path.endDirection(), kArrowColor);
        if (e.bidirectional) arrowHead(g->start, g->path.startDirection() * -1.0, kArrowColor);

        if (!e.label.empty()) {
            const FlatPath fp(g->path);
            const Vec2 pt = fp.pointAlong(e.labelPos.value_or(0.5), e.labelOff.value_or(10));
            text(pt, e.label, Font{e.labelSize.value_or(12), false}, kEdgeLabel, HAlign::Center, VAlign::Middle, 1,
                 solid(palette::kCanvasBg, 3));
        }
        if (selected)
            for (const Vec2 wp : e.waypoints)
                add(EllipseShape{wp, 6, 6}, Paint{kDark, solid(kWhite, 2), 1, Effect::None});
    }

    // ---- узлы ---------------------------------------------------------------
    void drawNode(const Node& n) {
        const ResolvedState st = resolveNodeState(stateAt(m_, n.id, t_));
        const auto hlIt = hl_.find(n.id);
        const Highlight* hl = hlIt != hl_.end() ? &hlIt->second : nullptr;
        const bool selected = o_.editorChrome && o_.selection.is(Selection::Kind::Node, n.id);
        const bool connectSrc = !o_.connectFromNode.empty() && o_.connectFromNode == n.id;

        std::optional<Transform> tr;
        if (hl && hl->scale != 1) tr = Transform{n.center(), 0, hl->scale, {}};

        const double rx = n.shape == "queue" ? 4 : 14;
        const Rect r = n.rect();
        const Color border = connectSrc ? kCyan : selected ? kWhite : st.ring;
        add(RectShape{r, rx}, Paint{st.fill, solid(border, selected || connectSrc ? 3 : 2), 1, Effect::Shadow}, tr);

        // акцентная полоса слева — по форме узла
        const Color accent = Color::parseOr(n.color, nodeKind(n.kind).color);
        add(RectShape{{n.x, n.y, 6, n.h}, 0}, fillOnly(accent, 0.9), tr, Clip{r, rx});

        if (n.shape == "db")
            add(EllipseShape{{n.x + n.w / 2, n.y + 10}, n.w / 2 - 6, 6}, strokeOnly(solid(st.ring, 1.5), 0.6), tr);

        const auto& kind = nodeKind(n.kind);
        text({n.x + 18, n.y + n.h / 2 + 6}, std::string(kind.icon), Font{16, false}, Color::rgb(0xe6eefc),
             HAlign::Left, VAlign::Baseline, 1, std::nullopt, tr);
        text({n.x + 40, n.y + n.h / 2 - 4}, n.label, Font{15, true}, Color::rgb(0xf2f6ff), HAlign::Left,
             VAlign::Baseline, 1, std::nullopt, tr);
        // приоритет подписи: своя подпись шага > подзаголовок узла (в «Норме») > метка состояния
        const std::string& sub = (st.id == "ok" && !st.customLabel && !n.subtitle.empty()) ? n.subtitle : st.label;
        text({n.x + 40, n.y + n.h / 2 + 14}, sub, Font{st.labelSize.value_or(11), false}, Color::rgb(0xb7c6e4),
             HAlign::Left, VAlign::Baseline, 0.85, std::nullopt, tr);

        for (const auto& port : n.ports) {
            const bool big = selected || o_.connectMode;
            const Color fill = o_.connectMode || selected ? kCyan : kDark;
            const Color ring = o_.connectMode ? Color::rgb(0xa5f3fc) : kCyan;
            add(EllipseShape{{n.x + port.dx, n.y + port.dy}, big ? 6.0 : 4.0, big ? 6.0 : 4.0},
                Paint{fill, solid(ring, 2), 1, Effect::None}, tr);
        }

        if (hl)
            add(RectShape{r.adjusted(6), rx + 6}, strokeOnly(solid(hl->ring, 3), hl->opacity, Effect::Glow), tr);
    }

    // ---- сообщение: пакет летит по связи ------------------------------------
    void drawMessage(const Step& s, double p) {
        const Node* a = m_.node(s.from);
        const Node* b = m_.node(s.to);
        if (!a || !b) return;
        const auto& variant = msgVariant(s.variant);
        const double ep = p < 0.5 ? 2 * p * p : 1 - std::pow(-2 * p + 2, 2) / 2;  // ease-in-out

        const Edge* edge = !s.edgeId.empty() ? m_.edge(s.edgeId) : m_.edgeBetween(s.from, s.to);
        Path path;
        Vec2 pos;
        Vec2 dir;
        if (const auto g = edge ? edgeGeometry(m_, *edge) : std::nullopt) {
            path = g->path;
            const FlatPath fp(path);
            const bool forward = edge->from == s.from;
            const double len = (forward ? ep : 1 - ep) * fp.length();
            pos = fp.pointAtLength(len);
            dir = fp.directionAt(len, forward);
        } else {
            // связи нет — прямая между границами узлов
            const Vec2 ca = a->center();
            const Vec2 cb = b->center();
            const Vec2 start = geom::borderPoint(ca, a->w / 2 + 3, a->h / 2 + 3, cb);
            const Vec2 end = geom::borderPoint(cb, b->w / 2 + 8, b->h / 2 + 8, ca);
            pos = lerp(start, end, ep);
            dir = end - start;
            dir = length(dir) > 0 ? dir / length(dir) : Vec2{1, 0};
            path = Path::line(start, end);
        }

        Stroke tail = solid(variant.color, 3);
        if (variant.dash > 0) tail.dash = {variant.dash, variant.gap};
        add(PathShape{path}, strokeOnly(tail, 0.25));

        const double ang = std::atan2(dir.y, dir.x) * 180 / std::numbers::pi;
        const Transform tr{{0, 0}, ang, 1, pos};
        add(RectShape{{-13, -8, 26, 16}, 8}, fillOnly(variant.color, 1, Effect::Glow), tr);
        Path tri;
        tri.moveTo({2, -4});
        tri.lineTo({8, 0});
        tri.lineTo({2, 4});
        tri.lineTo({2, -4});
        add(PathShape{std::move(tri)}, fillOnly(kDark, 0.85), tr);

        if (!s.label.empty()) {
            const Font font{11, true};
            const double tw = tm_.width(s.label, font) + 14;
            const Vec2 o{pos.x, pos.y - 16};
            add(RectShape{{o.x - tw / 2, o.y - 13, tw, 18}, 5}, Paint{kDark, solid(variant.color, 1), 0.82, Effect::None});
            text(o, s.label, font, variant.color, HAlign::Center);
        }
    }

    // ---- таймер: кольцо обратного отсчёта над узлом --------------------------
    void drawTimer(const Step& s, double p) {
        const Node* n = m_.node(s.nodeId);
        if (!n) return;
        const double total = s.seconds > 0 ? s.seconds : std::round(s.duration / 1000);
        const double remaining = std::max(0.0, total * (1 - p));
        const Vec2 c{n->x + n->w - 6, n->y - 6};
        constexpr double r = 20;
        const bool danger = remaining <= 1.5;
        add(EllipseShape{c, r + 4, r + 4}, fillOnly(kDark, 0.9));
        add(EllipseShape{c, r, r}, strokeOnly(solid(Color::rgb(0x334155), 4)));
        if (p < 1) {
            Stroke arc = solid(danger ? Color::rgb(0xef4444) : Color::rgb(0xf59e0b), 4);
            arc.roundCap = true;
            add(ArcShape{c, r, -90, 360 * (1 - p)}, strokeOnly(arc));
        }
        const std::string label =
            std::format("{}{}", static_cast<long long>(std::ceil(remaining)), timeUnit(s.unit).shortLabel);
        const auto len = utf8Length(label);
        const double fs = len >= 5 ? 10 : len == 4 ? 12 : 15;  // длинная подпись — мельче
        text({c.x, c.y + 5}, label, Font{fs, true}, danger ? Color::rgb(0xfca5a5) : Color::rgb(0xfcd34d),
             HAlign::Center);
        if (!s.label.empty())
            text({c.x, c.y + r + 15}, s.label, Font{10, false}, Color::rgb(0xcbd5e1), HAlign::Center);
    }

    // ---- заметка: плашка с плавным появлением/исчезновением -----------------
    void drawNote(const Step& s, double p) {
        const double fade = p < 0.12 ? p / 0.12 : p > 0.88 ? (1 - p) / 0.12 : 1;
        const std::string& txt = s.text.empty() ? kNoteDefault : s.text;
        const Font font{12.5, false};
        const double w = tm_.width(txt, font) + 28;
        const Color color = Color::parseOr(s.color, Color::rgb(0xfbbf24));
        const Rect r{s.x, s.y, w, 30};
        add(RectShape{r, 8}, Paint{Color::rgb(0x111a2e), solid(color, 1.5), fade, Effect::Shadow});
        add(RectShape{{s.x, s.y, 5, 30}, 0}, fillOnly(color, fade), std::nullopt, Clip{r, 8});
        text({s.x + 14, s.y + 19}, txt, font, Color::rgb(0xe6eefc), HAlign::Left, VAlign::Baseline, fade);
    }

    // ---- действие: бейдж со спиннером под узлом ------------------------------
    void drawAction(const Step& s) {
        const Node* n = m_.node(s.nodeId);
        if (!n) return;
        const Color color = Color::parseOr(s.color, Color::rgb(0x38bdf8));
        const std::string& txt = s.text.empty() ? kActionDefault : s.text;
        const Font font{12, true};
        const double tw = tm_.width(txt, font);
        constexpr double pad = 12, sr = 7, gap = 7;
        const double W = pad + sr * 2 + gap + tw + pad;
        const Vec2 c{n->x + n->w / 2, n->y + n->h + 18};
        const double left = c.x - W / 2;
        add(RectShape{{left, c.y - 13, W, 26}, 13}, Paint{kDark, solid(color, 1.2), 0.92, Effect::Shadow});
        const Vec2 sc{left + pad + sr, c.y};
        add(EllipseShape{sc, sr, sr}, strokeOnly(solid(color, 2.4), 0.25));
        Stroke spin = solid(color, 2.4);
        spin.roundCap = true;
        add(ArcShape{sc, sr, std::fmod(t_ / 1000 * 300, 360), 360 * 0.65}, strokeOnly(spin));
        text({sc.x + sr + gap, c.y + 4}, txt, font, color);
    }

    // ---- соединение: анимированная линия поверх связи + бейдж ---------------
    void drawLink(const Step& s) {
        const Edge* e = m_.edge(s.edgeId);
        const auto g = e ? edgeGeometry(m_, *e) : std::nullopt;
        if (!g) return;
        const Color color = Color::parseOr(s.color, Color::rgb(0xf87171));
        const auto& anim = linkAnim(s.anim);
        Stroke st = solid(color, 4);
        st.roundCap = true;
        double op = 0.9;
        if (anim.dashed) st.dash = {7, 6};
        if (anim.flow) st.dashOffset = std::fmod(-t_ / 1000 * 26, 1000);
        if (anim.pulse) {
            const double k = 0.5 + 0.5 * std::sin(t_ / 1000 * std::numbers::pi * 2);
            st.width = 3 + 2.5 * k;
            op = 0.45 + 0.45 * k;
        }
        add(PathShape{g->path}, strokeOnly(st, op));

        if (s.text.empty()) return;
        const double fs = s.labelSize.value_or(12);
        const FlatPath fp(g->path);
        const Vec2 pt = fp.pointAlong(s.labelPos.value_or(0.5), s.labelOff.value_or(22));
        const Font font{fs, true};
        const double tw = tm_.width(s.text, font);
        const double h = std::max(24.0, fs + 12);
        const double W = tw + std::max(12.0, fs) * 2;
        add(RectShape{{pt.x - W / 2, pt.y - h / 2, W, h}, h / 2}, Paint{kDark, solid(color, 1.2), 0.95, Effect::Shadow});
        text(pt, s.text, font, color, HAlign::Center, VAlign::Middle);
    }

    inline static const std::string kNoteDefault = "заметка";
    inline static const std::string kActionDefault = "Действие";

    const Model& m_;
    double t_;
    const SceneOptions& o_;
    const TextMeasurer& tm_;
    Highlights hl_;
    Frame f_;
};

}  // namespace

Frame buildFrame(const Model& m, double t, const SceneOptions& opt, const TextMeasurer& tm) {
    return Builder(m, t, opt, tm).build();
}

Rect contentBounds(const Model& m, const TextMeasurer& tm) {
    Bounds b;
    for (const auto& n : m.nodes) b.add(n.rect());
    for (const auto& e : m.edges)
        for (const Vec2 wp : e.waypoints) b.add(wp);
    for (const auto& s : m.scenario.steps) {
        if (s.type == StepType::Note) {
            const double w = tm.width(s.text.empty() ? "заметка" : s.text, Font{12.5, false}) + 28;
            b.add(Rect{s.x, s.y, w, 30});
        } else if (s.type == StepType::Action) {
            if (const Node* n = m.node(s.nodeId)) {
                const double w = tm.width(s.text, Font{12, true}) + 50;
                b.add(Rect{n->center().x - w / 2, n->y + n->h + 5, w, 26});
            }
        } else if (s.type == StepType::Link && !s.text.empty()) {
            if (const Edge* e = m.edge(s.edgeId))
                if (const auto g = edgeGeometry(m, *e)) {
                    const Vec2 pt = FlatPath(g->path).pointAlong(s.labelPos.value_or(0.5), s.labelOff.value_or(22));
                    const double fs = s.labelSize.value_or(12);
                    const double w = tm.width(s.text, Font{fs, true}) + std::max(12.0, fs) * 2;
                    const double h = std::max(24.0, fs + 12);
                    b.add(Rect{pt.x - w / 2, pt.y - h / 2, w, h});
                }
        }
    }
    return b.empty() ? Rect{0, 0, 640, 360} : b.rect();
}

}  // namespace ad
