#ifndef _COMMON_EXCEPTIONS_HPP_
#define _COMMON_EXCEPTIONS_HPP_

#include <stdexcept>
#include <string>

/// @file Exceptions.hpp
/// @brief Exception hierarchy of the animated-diagrams core library.
///
/// All exceptions derive from std::runtime_error so callers that only catch the
/// standard base type keep working. The header has no Qt dependencies.

namespace ad
{

/// Root exception for all animated-diagrams errors.
class AdError : public std::runtime_error
{
public:
    explicit AdError(const std::string &msg) : std::runtime_error(msg) {}
};

/// Malformed input data (JSON, XML, draw.io, plugin manifest, ...).
class ParseError : public AdError
{
public:
    explicit ParseError(const std::string &msg) : AdError("[Parse] " + msg) {}
};

/// File system / IO failure.
class IoError : public AdError
{
public:
    explicit IoError(const std::string &msg) : AdError("[IO] " + msg) {}
};

/// Invalid argument passed to a core API (unknown id, bad value, ...).
class InvalidArgument : public AdError
{
public:
    explicit InvalidArgument(const std::string &msg) : AdError("[Argument] " + msg) {}
};

} // namespace ad

#endif // _COMMON_EXCEPTIONS_HPP_
