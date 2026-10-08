#ifndef _IO_JSON_IO_HPP_
#define _IO_JSON_IO_HPP_

#include <expected>
#include <string>
#include <string_view>

#include "Model/Model.hpp"

/// @file JsonIo.hpp
/// @brief Reading / writing documents. Files of the web version (v1) and v2 load as is:
///        missing fields get defaults, dangling references are removed.

namespace ad
{

/// Parse and normalize a document. Error when the text is not JSON or has no nodes / scenario.
std::expected<Model, std::string> ParseModel(std::string_view json);

/// Bring a model into a consistent state: ids, sizes, references to nodes / edges / ports.
void NormalizeModel(Model &m);

std::string SerializeModel(const Model &m, int indent = 2);

} // namespace ad

#endif // _IO_JSON_IO_HPP_
