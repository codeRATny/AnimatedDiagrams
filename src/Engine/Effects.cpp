#include "Effects.hpp"

#include <algorithm>
#include <cmath>

namespace ad
{

void EffectState::Combine(const EffectState &o)
{
    opacity *= o.opacity;
    scale *= o.scale;
    rotate += o.rotate;
    dx += o.dx;
    dy += o.dy;
    if (std::max(o.glow, o.tint) > std::max(glow, tint))
    {
        color = o.color;
    }
    glow = std::max(glow, o.glow);
    tint = std::max(tint, o.tint);
}

bool EffectState::IsIdentity() const { return opacity == 1 && scale == 1 && rotate == 0 && dx == 0 && dy == 0 && glow == 0 && tint == 0; }

double EvaluateTrack(const EffectTrack &track, double t)
{
    if (track.keys.empty())
    {
        return DefaultValue(track.property);
    }
    std::vector<Keyframe> keys = track.keys;
    std::ranges::stable_sort(keys, {}, &Keyframe::t);
    if (t <= keys.front().t)
    {
        return keys.front().value;
    }
    if (t >= keys.back().t)
    {
        return keys.back().value;
    }
    for (size_t i = 1; i < keys.size(); ++i)
    {
        if (t <= keys[i].t)
        {
            const Keyframe &a    = keys[i - 1];
            const Keyframe &b    = keys[i];
            const double    span = b.t - a.t;
            const double    u    = span > 0 ? (t - a.t) / span : 1.0;
            return a.value + (b.value - a.value) * ApplyEasing(b.easing, u);
        }
    }
    return keys.back().value;
}

EffectState EvaluateEffect(const EffectDef &effect, double progress, int repeat_override, double intensity, const Color *color_override)
{
    EffectState st;
    st.color         = color_override != nullptr ? *color_override : Color::Parse(effect.color, st.color);
    const int    n   = std::max(1, repeat_override > 0 ? repeat_override : effect.repeat);
    const double p   = std::clamp(progress, 0.0, 1.0);
    const double raw = p * n;
    // the very end of the step is the end of the last repetition, not the start of a new one
    const double local = p >= 1.0 ? 1.0 : raw - std::floor(raw);

    for (const auto &track : effect.tracks)
    {
        const double def = DefaultValue(track.property);
        const double v   = def + (EvaluateTrack(track, local) - def) * intensity;
        switch (track.property)
        {
        case EffectProperty::Opacity:
            st.opacity = std::clamp(v, 0.0, 1.0);
            break;
        case EffectProperty::Scale:
            st.scale = std::max(0.0, v);
            break;
        case EffectProperty::Rotate:
            st.rotate = v;
            break;
        case EffectProperty::OffsetX:
            st.dx = v;
            break;
        case EffectProperty::OffsetY:
            st.dy = v;
            break;
        case EffectProperty::Glow:
            st.glow = std::clamp(v, 0.0, 1.0);
            break;
        case EffectProperty::Tint:
            st.tint = std::clamp(v, 0.0, 1.0);
            break;
        }
    }
    return st;
}

} // namespace ad
