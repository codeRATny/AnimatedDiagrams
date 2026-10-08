#ifndef _MODEL_STEP_HPP_
#define _MODEL_STEP_HPP_

#include <optional>
#include <string>

#include "Model/Catalog.hpp"
#include "Model/Easing.hpp"

/// @file Step.hpp
/// @brief A step of the scenario (one bar on the timeline).

namespace ad
{

inline constexpr double kMinStepDuration = 100;

/// Scenario step. Fields are shared by all step types: values survive a type
/// change (as in the web version); only the fields relevant to the type are
/// written to JSON.
struct Step
{
    std::string id;
    StepType    type     = StepType::Message;
    double      start    = 0; // ms
    double      duration = 1200;

    // message
    std::string from;
    std::string to;
    std::string edge_id; // message, link
    std::string variant      = "request";
    std::string packet       = "capsule"; // packet shape
    double      packet_size  = 1;         // packet scale
    int         packet_count = 1;         // > 1: a stream of packets
    Easing      easing       = Easing::EaseInOut;
    bool        trail        = true; // highlight the path under the packet

    // timer, state, action, effect
    std::string node_id;
    std::string label; // message, timer, state
    std::string text;  // note, action, link
    std::string color; // state, note, action, link, effect, message (override)

    // timer
    double      seconds = 0; // countdown start (0 -- derived from the duration)
    std::string unit    = "s";

    // state
    std::string state = "down";

    std::optional<double> label_size; // state, link
    std::optional<double> label_pos;  // link
    std::optional<double> label_off;  // link

    // note
    double x = 0;
    double y = 0;

    // link
    std::string anim = "flow";

    // effect
    std::string effect    = "pulse";
    double      intensity = 1; // multiplier of the effect amplitude
    int         repeat    = 0; // 0 -- use the effect's own repeat count

    [[nodiscard]] double End() const { return start + duration; }
    friend bool          operator==(const Step &, const Step &) = default;
};

} // namespace ad

#endif // _MODEL_STEP_HPP_
