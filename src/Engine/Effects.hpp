#ifndef _ENGINE_EFFECTS_HPP_
#define _ENGINE_EFFECTS_HPP_

#include "Model/Color.hpp"
#include "Model/Library.hpp"

/// @file Effects.hpp
/// @brief Keyframe effect evaluation.

namespace ad
{

/// Combined visual state of a node produced by effects at a moment of time.
struct EffectState
{
    double opacity = 1;
    double scale   = 1;
    double rotate  = 0; // degrees
    double dx      = 0;
    double dy      = 0;
    double glow    = 0; // 0..1
    double tint    = 0; // 0..1
    Color  color   = Color::Rgb(0x22d3ee);

    /// Accumulate another effect: multiplicative opacity/scale, additive rotation/offsets,
    /// max glow/tint (the color follows the strongest glow or tint).
    void               Combine(const EffectState &other);
    [[nodiscard]] bool IsIdentity() const;
};

/// Value of a track at t in [0, 1]; keys are expected sorted by t (unsorted input is tolerated).
double EvaluateTrack(const EffectTrack &track, double t);

/// Evaluate an effect at `progress` in [0, 1] over the step duration.
/// repeat_override > 0 replaces the effect's repeat count; intensity scales the
/// deviation of every property from its default value.
EffectState EvaluateEffect(const EffectDef &effect, double progress, int repeat_override = 0, double intensity = 1,
                           const Color *color_override = nullptr);

} // namespace ad

#endif // _ENGINE_EFFECTS_HPP_
