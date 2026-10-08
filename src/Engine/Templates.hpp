#ifndef _ENGINE_TEMPLATES_HPP_
#define _ENGINE_TEMPLATES_HPP_

#include <map>
#include <string>
#include <vector>

#include "Model/Document.hpp"

/// @file Templates.hpp
/// @brief Animation templates: inserting a template into the scenario and
///        creating a template from existing steps.

namespace ad
{

/// Insert the template steps at `start` (ms) with roles mapped to node ids.
/// One history entry. Link steps whose edge does not exist between the mapped
/// nodes are skipped. Returns the ids of the inserted steps.
/// Throws ad::InvalidArgument when a role is not mapped to an existing node.
std::vector<std::string> ApplyTemplate(Document &doc, const AnimationTemplate &tpl, const std::map<std::string, std::string> &roles,
                                       double start);

/// Build a template from the given steps: referenced nodes become roles (labelled
/// with the node labels), times are shifted so the first step starts at 0.
AnimationTemplate MakeTemplate(const Model &m, const std::vector<std::string> &step_ids, const std::string &id, const std::string &label);

/// Ids of the steps that start inside [from, to] ms.
std::vector<std::string> StepsInRange(const Model &m, double from, double to);

} // namespace ad

#endif // _ENGINE_TEMPLATES_HPP_
