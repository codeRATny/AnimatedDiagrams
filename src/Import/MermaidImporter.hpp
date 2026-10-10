#ifndef _IMPORT_MERMAID_IMPORTER_HPP_
#define _IMPORT_MERMAID_IMPORTER_HPP_

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "Model/Model.hpp"

/// @file MermaidImporter.hpp
/// @brief Import of Mermaid diagrams (https://mermaid.js.org) into a document.
///
/// The text is parsed by merman (a Rust implementation of Mermaid, through its C ABI): the
/// importer only maps Mermaid's semantic model onto ours.
///  - flowchart / graph: nodes (the element type is chosen by shape), edges with labels, arrow
///    heads and line styles, `style` / `classDef` colors; positions come from Mermaid's own layout.
///  - sequenceDiagram: participants become nodes in a row, messages become message steps along
///    (created) edges, played one after another; replies, errors and async messages get their
///    message kinds, self messages become actions, notes become notes, activations become the
///    "Active" state, `par` branches run in parallel, `loop` / `alt` / `opt` / `critical` /
///    `break` / `rect` sections and "Note over ...: ⚑ label" become timeline markers.
///  - Markdown: the ```mermaid blocks are read; a flowchart and a sequence diagram together
///    (what the Mermaid export writes) give the structure and the scenario of one document.

namespace ad
{

struct MermaidImportOptions
{
    bool keep_colors = true; // keep style / classDef colors
};

struct MermaidImportReport
{
    std::string              kinds; // "flowchart", "sequence", "flowchart + sequence"
    int                      nodes   = 0;
    int                      edges   = 0;
    int                      steps   = 0;
    int                      markers = 0;
    int                      skipped = 0; // unsupported statements (subgraph frames, links, ...)
    std::vector<std::string> warnings;
};

/// The build includes the Mermaid parser (WITH_MERMAID).
bool MermaidImportAvailable();

/// Mermaid sources of a text: the ```mermaid / ~~~mermaid blocks of Markdown, otherwise the whole text.
std::vector<std::string> MermaidBlocks(std::string_view text);

/// Convert Mermaid text (a diagram or Markdown with ```mermaid blocks) into a normalized model.
/// Error text on failure (syntax errors carry Mermaid's line and column).
std::expected<Model, std::string> ImportMermaid(std::string_view text, const MermaidImportOptions &opt = {},
                                                MermaidImportReport *report = nullptr);

} // namespace ad

#endif // _IMPORT_MERMAID_IMPORTER_HPP_
