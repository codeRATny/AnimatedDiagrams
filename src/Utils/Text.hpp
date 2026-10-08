#ifndef _UTILS_TEXT_HPP_
#define _UTILS_TEXT_HPP_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// @file Text.hpp
/// @brief String helpers: base64, URL decoding, HTML to plain text, slugs.

namespace ad
{

std::string Base64Encode(std::span<const uint8_t> data);
std::string Base64Encode(std::string_view data);
/// Whitespace is ignored. Throws ad::ParseError on invalid input.
std::vector<uint8_t> Base64Decode(std::string_view text);

/// Percent-decoding ("%D0%90" -> UTF-8); '+' is kept as is (encodeURIComponent semantics).
std::string UrlDecode(std::string_view s);

/// Decode HTML entities (&amp; &lt; &gt; &quot; &#39; &nbsp; &#NNN; &#xHH;) into UTF-8.
std::string DecodeHtmlEntities(std::string_view s);

/// Plain text from an HTML fragment: <br> and block ends become line breaks,
/// tags are dropped, entities decoded, whitespace collapsed.
std::string HtmlToText(std::string_view html);

std::string Trim(std::string_view s);

/// Lowercase ASCII identifier from arbitrary text ("Мой Эффект 2" -> "effect-2" style slugs; non-ASCII dropped).
std::string Slugify(std::string_view s, std::string_view fallback = "item");

/// Encode a code point as UTF-8.
void AppendUtf8(std::string &out, uint32_t cp);

} // namespace ad

#endif // _UTILS_TEXT_HPP_
