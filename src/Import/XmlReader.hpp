#ifndef _IMPORT_XML_READER_HPP_
#define _IMPORT_XML_READER_HPP_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// @file XmlReader.hpp
/// @brief Minimal XML DOM parser (elements, attributes, text, CDATA, entities).
///        Enough for draw.io / mxGraph files; no DTD or namespace processing.

namespace ad
{

struct XmlNode
{
    std::string                                      name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<XmlNode>                             children;
    std::string                                      text; // concatenated character data (entities decoded)

    [[nodiscard]] const std::string           *Attr(std::string_view key) const;
    [[nodiscard]] std::string                  AttrOr(std::string_view key, std::string_view def = {}) const;
    [[nodiscard]] const XmlNode               *Child(std::string_view child_name) const;
    [[nodiscard]] std::vector<const XmlNode *> ChildrenNamed(std::string_view child_name) const;
};

/// Parse a document and return its root element. Throws ad::ParseError.
XmlNode ParseXml(std::string_view text);

} // namespace ad

#endif // _IMPORT_XML_READER_HPP_
