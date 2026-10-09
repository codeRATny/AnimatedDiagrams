#include "HtmlPlayer.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace ad
{

std::string EscapeJsonForScript(std::string_view json)
{
    std::string out;
    out.reserve(json.size() + 64);
    for (const char c : json)
    {
        if (c == '<')
        {
            out += "\\u003c";
        }
        else
        {
            out += c;
        }
    }
    return out;
}

std::string EscapeHtml(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 16);
    for (const char c : text)
    {
        switch (c)
        {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&#39;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

std::string PlayerOptionsJson(const PlayerOptions &options)
{
    return std::format(R"({{"autoplay":{},"loop":{}}})", options.autoplay, options.loop);
}

std::expected<std::string, std::string> MakePlayerHtml(std::string_view html_template, std::string_view document_json,
                                                       std::string_view title, const PlayerOptions &options)
{
    struct Slot
    {
        std::string_view placeholder;
        std::string      value;
        size_t           pos = std::string_view::npos;
    };
    std::array<Slot, 3> slots{{
        {kPlayerTitlePlaceholder, EscapeHtml(title.empty() ? std::string_view("Animated diagram") : title)},
        {kPlayerOptionsPlaceholder, PlayerOptionsJson(options)},
        {kPlayerDocumentPlaceholder, EscapeJsonForScript(document_json)},
    }};
    // positions in the template only: injected values are never searched again
    for (Slot &s : slots)
    {
        s.pos = html_template.find(s.placeholder);
        if (s.pos == std::string_view::npos)
        {
            return std::unexpected(std::format("not an HTML player template: {} is missing", s.placeholder));
        }
    }
    std::ranges::sort(slots, {}, &Slot::pos);

    std::string out;
    out.reserve(html_template.size() + document_json.size() + 256);
    size_t at = 0;
    for (const Slot &s : slots)
    {
        if (s.pos < at)
        {
            return std::unexpected(std::string("overlapping placeholders in the HTML player template"));
        }
        out.append(html_template.substr(at, s.pos - at));
        out += s.value;
        at = s.pos + s.placeholder.size();
    }
    out.append(html_template.substr(at));
    return out;
}

} // namespace ad
