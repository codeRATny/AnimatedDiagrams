#ifndef _UTILS_FILE_HPP_
#define _UTILS_FILE_HPP_

#include <filesystem>
#include <string>
#include <string_view>

/// @file File.hpp
/// @brief Small file helpers (binary-safe, UTF-8 paths).

namespace ad
{

/// Whole file as bytes. Throws ad::IoError.
std::string ReadFile(const std::filesystem::path &path);

/// Write via a temporary file and rename (no truncated files on failure). Throws ad::IoError.
void WriteFile(const std::filesystem::path &path, std::string_view data);

/// Path from a UTF-8 string (portable across Windows / POSIX).
std::filesystem::path PathFromUtf8(std::string_view utf8);
std::string           PathToUtf8(const std::filesystem::path &p);

} // namespace ad

#endif // _UTILS_FILE_HPP_
