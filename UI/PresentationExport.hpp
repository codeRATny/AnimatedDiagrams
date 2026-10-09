#ifndef _UI_PRESENTATION_EXPORT_HPP_
#define _UI_PRESENTATION_EXPORT_HPP_

#include <expected>
#include <stop_token>
#include <utility>
#include <vector>

#include "Exporter.hpp"

/// @file PresentationExport.hpp
/// @brief PowerPoint export: renders clips (video / GIF) for media slides with the Qt
///        renderer and writes the presentation with the core PPTX writer (src/Export/Pptx.hpp).

namespace ad::ui
{

/// [start, end) of the slides: scenario segments between markers or the whole scenario.
std::vector<std::pair<double, double>> PresentationSegments(const Model &m, bool by_markers);

std::expected<ExportResult, QString> ExportPresentation(const Model &m, const ExportOptions &o, const Registry &reg, std::stop_token stop,
                                                        const ProgressFn &progress = {});

} // namespace ad::ui

#endif // _UI_PRESENTATION_EXPORT_HPP_
