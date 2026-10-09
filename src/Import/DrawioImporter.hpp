#ifndef _IMPORT_DRAWIO_IMPORTER_HPP_
#define _IMPORT_DRAWIO_IMPORTER_HPP_

#include <cstddef>
#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Model/Model.hpp"

/// @file DrawioImporter.hpp
/// @brief Import of draw.io / diagrams.net files (.drawio, .xml) into a document.
///
/// Supported: plain and compressed (deflate + base64) pages, vertices with common
/// shapes (rectangle, rounded, ellipse, rhombus, cylinder, hexagon, parallelogram,
/// document, cloud, note, actor), groups / containers (coordinates are made absolute),
/// edges with labels, waypoints, routing and arrows, free text as notes.
/// Colors, dashes, font sizes are kept as node / edge style overrides.

namespace ad
{

struct DrawioImportOptions
{
    int    page              = 0;                    // page index in a multi-page file
    bool   keep_colors       = true;                 // keep fill / stroke / font colors from draw.io
    size_t max_inflated_size = 256U * 1024U * 1024U; // limit of a decompressed page (decompression bomb guard)
};

struct DrawioImportReport
{
    int                      nodes   = 0;
    int                      edges   = 0;
    int                      notes   = 0;
    int                      skipped = 0; // containers, dangling edges, unsupported cells
    std::vector<std::string> warnings;
};

/// Names of the pages ("Page-1", ...). Throws ad::ParseError.
std::vector<std::string> DrawioPageNames(std::string_view file_content);

/// Name given to an imported document whose page has no name (callers may replace it with the file name).
std::string DrawioDefaultName();

/// Decode a draw.io style string "rounded=1;fillColor=#fff;ellipse;" into key/value pairs
/// (bare tokens get the value "1"; the first bare token is also stored as "shape" if absent).
std::map<std::string, std::string> ParseDrawioStyle(std::string_view style);

/// Convert a page into a normalized model. Error text on failure.
std::expected<Model, std::string> ImportDrawio(std::string_view file_content, const DrawioImportOptions &opt = {},
                                               DrawioImportReport *report = nullptr);

} // namespace ad

#endif // _IMPORT_DRAWIO_IMPORTER_HPP_
