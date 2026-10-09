#ifndef _IO_JSON_CODEC_HPP_
#define _IO_JSON_CODEC_HPP_

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Model/Model.hpp"

/// @file JsonCodec.hpp
/// @brief Conversions between model types and JSON (camelCase keys, compatible with
///        files of the web version). Readers are tolerant: wrong field types fall back
///        to defaults, invalid items are skipped and reported through `warnings`.

namespace ad::json
{

using Json        = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;
using Warnings    = std::vector<std::string>;

// ---- readers ---------------------------------------------------------------
NodeStyle                        NodeStyleFromJson(const Json &j);
EdgeStyle                        EdgeStyleFromJson(const Json &j);
Node                             NodeFromJson(const Json &j);
Edge                             EdgeFromJson(const Json &j);
std::optional<Step>              StepFromJson(const Json &j);
std::optional<ElementType>       ElementFromJson(const Json &j, Warnings *warnings = nullptr);
std::optional<EffectDef>         EffectFromJson(const Json &j, Warnings *warnings = nullptr);
std::optional<AnimationTemplate> AnimationFromJson(const Json &j, Warnings *warnings = nullptr);
std::optional<DesignSystem>      DesignSystemFromJson(const Json &j, Warnings *warnings = nullptr);
LibrarySet                       LibraryFromJson(const Json &j, Warnings *warnings = nullptr);
/// Whole model without normalization (see NormalizeModel in JsonIo.hpp).
Model ModelFromJson(const Json &j);

// ---- writers ---------------------------------------------------------------
OrderedJson ToJson(const NodeStyle &s);
OrderedJson ToJson(const EdgeStyle &s);
OrderedJson ToJson(const Node &n);
OrderedJson ToJson(const Edge &e);
OrderedJson ToJson(const Step &s);
OrderedJson ToJson(const ElementType &e);
OrderedJson ToJson(const EffectDef &e);
OrderedJson ToJson(const AnimationTemplate &a);
OrderedJson ToJson(const DesignSystem &d);
OrderedJson ToJson(const LibrarySet &l);
OrderedJson ToJson(const Model &m);

} // namespace ad::json

#endif // _IO_JSON_CODEC_HPP_
