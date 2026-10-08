#include "PreviewWidget.hpp"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <set>

#include "Engine/Scene.hpp"
#include "Engine/Templates.hpp"
#include "Model/Document.hpp"
#include "QtRender.hpp"

namespace ad::ui
{

namespace
{

constexpr double kEffectStep = 1600; // ms
constexpr double kLoopPause  = 600;

Node MakeNode(const std::string &id, const std::string &label, const std::string &type, double x, double y, double w, double h)
{
    Node n;
    n.id    = id;
    n.label = label;
    n.type  = type;
    n.x     = x;
    n.y     = y;
    n.w     = w;
    n.h     = h;
    return n;
}

} // namespace

PreviewWidget::PreviewWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(260, 160);
    setAttribute(Qt::WA_OpaquePaintEvent);
    _timer.setInterval(33);
    connect(&_timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
}

void PreviewWidget::Clear()
{
    _has_scene = false;
    _timer.stop();
    update();
}

void PreviewWidget::_SetScene(Model m, const Registry &reg, bool animated)
{
    _model     = std::move(m);
    _registry  = reg;
    _has_scene = true;
    _animated  = animated;
    if (animated)
    {
        _clock.start();
        _timer.start();
    }
    else
    {
        _timer.stop();
    }
    update();
}

void PreviewWidget::ShowElement(const ElementType &type, const Registry &reg)
{
    Model m;
    m.library.Upsert(type);
    m.nodes.push_back(MakeNode("n1", type.label, type.id, 0, 0, type.width, type.height));
    m.scenario.duration = 1000;
    _SetScene(std::move(m), reg, false);
}

void PreviewWidget::ShowEffect(const EffectDef &effect, const Registry &reg)
{
    Model m;
    m.library.Upsert(effect);
    m.nodes.push_back(MakeNode("n1", effect.label, "service", 0, 0, 150, 64));
    Step s;
    s.id       = "s1";
    s.type     = StepType::Effect;
    s.node_id  = "n1";
    s.effect   = effect.id;
    s.start    = 0;
    s.duration = kEffectStep * std::max(1, effect.repeat);
    m.scenario.steps.push_back(s);
    m.scenario.duration = s.duration + kLoopPause;
    _SetScene(std::move(m), reg, true);
}

Model PreviewWidget::AnimationScene(const AnimationTemplate &tpl)
{
    Model                              m;
    std::map<std::string, std::string> roles;
    double                             x = 0;
    for (const auto &r : tpl.roles)
    {
        const std::string id = "n_" + r.id;
        m.nodes.push_back(MakeNode(id, r.label.empty() ? r.id : r.label, "service", x, 0, 140, 64));
        roles[r.id] = id;
        x += 230;
    }
    // edges for every role pair used by messages and links, so packets follow lines
    std::set<std::pair<std::string, std::string>> pairs;
    for (const auto &s : tpl.steps)
    {
        if (s.type == StepType::Message && roles.contains(s.from) && roles.contains(s.to))
        {
            pairs.emplace(std::min(s.from, s.to), std::max(s.from, s.to));
        }
        if (s.type == StepType::Link)
        {
            const auto gt = s.edge_id.find('>');
            if (gt != std::string::npos)
            {
                const std::string a = s.edge_id.substr(0, gt);
                const std::string b = s.edge_id.substr(gt + 1);
                if (roles.contains(a) && roles.contains(b))
                {
                    pairs.emplace(std::min(a, b), std::max(a, b));
                }
            }
        }
    }
    int k = 0;
    for (const auto &[a, b] : pairs)
    {
        Edge e;
        e.id   = "e" + std::to_string(++k);
        e.from = roles[a];
        e.to   = roles[b];
        m.edges.push_back(e);
    }
    m.scenario.steps.clear();
    m.scenario.duration = 1000;
    Document doc(m);
    try
    {
        ApplyTemplate(doc, tpl, roles, 0);
    }
    catch (const std::exception &)
    {
        // an incomplete template still previews its nodes
    }
    Model  out = doc.Get();
    double end = 0;
    for (const auto &s : out.scenario.steps)
    {
        end = std::max(end, s.End());
    }
    out.scenario.duration = std::max(1000.0, end + kLoopPause);
    return out;
}

void PreviewWidget::ShowAnimation(const AnimationTemplate &tpl, const Registry &reg)
{
    Model m = AnimationScene(tpl);
    m.library.Upsert(tpl);
    _SetScene(std::move(m), reg, true);
}

void PreviewWidget::paintEvent(QPaintEvent * /*e*/)
{
    QPainter p(this);
    p.fillRect(rect(), ToQColor(palette::kCanvasBg));
    p.setPen(QPen(QColor(0x22, 0x31, 0x4f), 1));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    if (!_has_scene)
    {
        p.setPen(QColor(0x85, 0x98, 0xb8));
        p.drawText(rect(), Qt::AlignCenter, tr("Нет предпросмотра"));
        return;
    }
    const double t = _animated ? std::fmod(static_cast<double>(_clock.elapsed()), std::max(1.0, _model.scenario.duration)) : 0;
    SceneOptions opt;
    opt.editor_chrome = false;
    const QtTextMeasurer tm;
    const Rect           r    = ContentBounds(_model, tm, _registry);
    constexpr double     kPad = 24;
    const double         zoom =
        std::clamp(std::min((width() - kPad * 2) / std::max(1.0, r.w), (height() - kPad * 2) / std::max(1.0, r.h)), 0.2, 2.0);
    const Vec2 c = r.Center();
    p.save();
    p.translate(width() / 2.0 - c.x * zoom, height() / 2.0 - c.y * zoom);
    p.scale(zoom, zoom);
    RenderFrame(p, BuildFrame(_model, t, opt, tm, _registry));
    p.restore();
}

} // namespace ad::ui
