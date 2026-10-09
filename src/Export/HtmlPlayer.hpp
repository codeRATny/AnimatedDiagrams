#ifndef _EXPORT_HTML_PLAYER_HPP_
#define _EXPORT_HTML_PLAYER_HPP_

#include <expected>
#include <string>
#include <string_view>

/// @file HtmlPlayer.hpp
/// @brief Filling the HTML player template (player/player.html, built with Emscripten):
///        the document JSON, player options and the page title are injected into
///        placeholders; the result is one self-contained HTML file.

namespace ad
{

/// Placeholders of the template; each occurs exactly once.
inline constexpr std::string_view kPlayerTitlePlaceholder    = "{{AD_TITLE}}";
inline constexpr std::string_view kPlayerOptionsPlaceholder  = "{{AD_PLAYER_OPTIONS}}";
inline constexpr std::string_view kPlayerDocumentPlaceholder = "{{AD_DOCUMENT_JSON}}";

struct PlayerOptions
{
    bool autoplay = true;
    bool loop     = true;
};

/// JSON text made safe inside <script type="application/json">: every '<' becomes "\u003c"
/// ('<' only occurs inside JSON strings, so the value is unchanged), which rules out
/// "</script>" and "<!--" in the element content.
std::string EscapeJsonForScript(std::string_view json);

/// Text escaped for HTML element content and attribute values (& < > " ').
std::string EscapeHtml(std::string_view text);

/// Player options as the JSON object the player reads ({"autoplay":true,"loop":true}).
std::string PlayerOptionsJson(const PlayerOptions &options);

/// The template with the placeholders replaced by the escaped title, options and document.
/// Error when the template lacks a placeholder (not a player template).
std::expected<std::string, std::string> MakePlayerHtml(std::string_view html_template, std::string_view document_json,
                                                       std::string_view title, const PlayerOptions &options = {});

} // namespace ad

#endif // _EXPORT_HTML_PLAYER_HPP_
