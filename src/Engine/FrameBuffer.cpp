#include "FrameBuffer.hpp"

#include <variant>

namespace ad
{

namespace
{

template <class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};

constexpr unsigned kFlagFill      = 1U;
constexpr unsigned kFlagStroke    = 2U;
constexpr unsigned kFlagTransform = 4U;
constexpr unsigned kFlagClip      = 8U;

double Num(bool b) { return b ? 1.0 : 0.0; }

ShapeTag TagOf(const Shape &shape)
{
    return std::visit(
        Overloaded{
            [](const RectShape &)
            {
                return ShapeTag::Rect;
            },
            [](const EllipseShape &)
            {
                return ShapeTag::Ellipse;
            },
            [](const PathShape &)
            {
                return ShapeTag::Path;
            },
            [](const ArcShape &)
            {
                return ShapeTag::Arc;
            },
            [](const TextShape &)
            {
                return ShapeTag::Text;
            },
        },
        shape);
}

} // namespace

void FrameBuffer::Encode(const Frame &frame)
{
    _data.clear();
    _strings.clear();
    _data.push_back(kFrameBufferVersion);
    _data.push_back(static_cast<double>(frame.items.size()));
    for (const Item &it : frame.items)
    {
        _Item(it);
    }
}

void FrameBuffer::_Color(Color c) { _data.push_back(static_cast<double>((uint32_t{c.r} << 16U) | (uint32_t{c.g} << 8U) | c.b)); }

void FrameBuffer::_Stroke(const Stroke &s)
{
    _Color(s.color);
    _data.push_back(s.width);
    _data.push_back(Num(s.round_cap));
    _data.push_back(s.dash_offset);
    _data.push_back(static_cast<double>(s.dash.size()));
    _data.insert(_data.end(), s.dash.begin(), s.dash.end());
}

void FrameBuffer::_Path(const Path &p)
{
    const auto &segs = p.Segments();
    _data.push_back(static_cast<double>(segs.size()));
    for (const auto &s : segs)
    {
        _data.push_back(static_cast<double>(static_cast<int>(s.kind)));
        switch (s.kind)
        {
        case Path::Kind::Move:
        case Path::Kind::Line:
            _data.insert(_data.end(), {s.to.x, s.to.y});
            break;
        case Path::Kind::Quad:
            _data.insert(_data.end(), {s.c1.x, s.c1.y, s.to.x, s.to.y});
            break;
        case Path::Kind::Cubic:
            _data.insert(_data.end(), {s.c1.x, s.c1.y, s.c2.x, s.c2.y, s.to.x, s.to.y});
            break;
        case Path::Kind::Close:
            break;
        }
    }
}

void FrameBuffer::_String(const std::string &s)
{
    _data.push_back(static_cast<double>(_strings.size()));
    _data.push_back(static_cast<double>(s.size()));
    _strings += s;
}

void FrameBuffer::_Item(const Item &it)
{
    const Paint &paint = it.paint;
    unsigned     flags = 0;
    flags |= paint.fill.has_value() ? kFlagFill : 0U;
    flags |= paint.stroke.has_value() ? kFlagStroke : 0U;
    flags |= it.transform.has_value() ? kFlagTransform : 0U;
    flags |= it.clip.has_value() ? kFlagClip : 0U;

    _data.push_back(static_cast<double>(static_cast<int>(TagOf(it.shape))));
    _data.push_back(paint.opacity);
    _data.push_back(static_cast<double>(static_cast<int>(paint.effect)));
    _data.push_back(static_cast<double>(flags));
    if (paint.fill.has_value())
    {
        _Color(*paint.fill);
    }
    if (paint.stroke.has_value())
    {
        _Stroke(*paint.stroke);
    }
    if (it.transform.has_value())
    {
        const Transform &t = *it.transform;
        _data.insert(_data.end(), {t.origin.x, t.origin.y, t.rotate_deg, t.scale, t.translate.x, t.translate.y});
    }
    if (it.clip.has_value())
    {
        _Path(*it.clip);
    }

    std::visit(
        Overloaded{
            [this](const RectShape &s)
            {
                _data.insert(_data.end(), {s.rect.x, s.rect.y, s.rect.w, s.rect.h, s.radius});
            },
            [this](const EllipseShape &s)
            {
                _data.insert(_data.end(), {s.center.x, s.center.y, s.rx, s.ry});
            },
            [this](const PathShape &s)
            {
                _Path(s.path);
            },
            [this](const ArcShape &s)
            {
                _data.insert(_data.end(), {s.center.x, s.center.y, s.radius, s.start_deg, s.sweep_deg});
            },
            [this](const TextShape &s)
            {
                _data.insert(_data.end(), {s.pos.x, s.pos.y});
                _String(s.text);
                _data.push_back(s.font.size);
                _data.push_back(Num(s.font.bold));
                _String(s.font.family);
                _data.push_back(static_cast<double>(static_cast<int>(s.align)));
                _data.push_back(static_cast<double>(static_cast<int>(s.valign)));
                _data.push_back(Num(s.halo.has_value()));
                if (s.halo.has_value())
                {
                    _Color(s.halo->color);
                    _data.push_back(s.halo->width);
                }
            },
        },
        it.shape);
}

} // namespace ad
