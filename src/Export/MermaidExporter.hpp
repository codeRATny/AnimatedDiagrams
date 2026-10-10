#ifndef _EXPORT_MERMAID_EXPORTER_HPP_
#define _EXPORT_MERMAID_EXPORTER_HPP_

#include <optional>
#include <string>
#include <string_view>

#include "Model/Model.hpp"
#include "Model/Registry.hpp"

/// @file MermaidExporter.hpp
/// @brief Export of a document as Mermaid text (https://mermaid.js.org): renders on GitHub,
///        GitLab, in Notion, Obsidian, Confluence and in documentation generators.
///
///  - flowchart: nodes (shape by the element type), edges with labels, arrow heads, line styles
///    and colors; "%% ad:pos" comments keep the exact positions for a re-import.
///  - sequenceDiagram: the scenario -- participants (nodes that take part, left to right),
///    messages with arrows by kind, actions as self messages, notes, timers ("⏱"), states ("●"),
///    markers ("⚑") and overlapping steps as `par` blocks. Visual-only steps (effects, edge
///    animations) are written as comments.
///  - Markdown: both diagrams in ```mermaid blocks (the Mermaid import reads them back).

namespace ad
{

enum class MermaidKind
{
    Flowchart,
    Sequence,
    Markdown // both, in ```mermaid blocks
};

std::string_view           MermaidKindId(MermaidKind k); // flowchart | sequence | markdown
std::optional<MermaidKind> MermaidKindFromId(std::string_view id);

std::string ExportMermaidFlowchart(const Model &m, const Registry &reg);
std::string ExportMermaidSequence(const Model &m);
std::string ExportMermaid(const Model &m, const Registry &reg, MermaidKind kind);

} // namespace ad

#endif // _EXPORT_MERMAID_EXPORTER_HPP_
