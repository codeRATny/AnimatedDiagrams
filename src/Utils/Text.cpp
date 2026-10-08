#include "Text.hpp"

#include <array>
#include <cctype>
#include <charconv>

#include "Common/Exceptions.hpp"

namespace ad
{

namespace
{

constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int Base64Value(char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z')
    {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9')
    {
        return c - '0' + 52;
    }
    if (c == '+' || c == '-')
    {
        return 62;
    }
    if (c == '/' || c == '_')
    {
        return 63;
    }
    return -1;
}

int HexValue(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

bool IsSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

} // namespace

void AppendUtf8(std::string &out, uint32_t cp)
{
    if (cp < 0x80U)
    {
        out += static_cast<char>(cp);
    }
    else if (cp < 0x800U)
    {
        out += static_cast<char>(0xC0U | (cp >> 6U));
        out += static_cast<char>(0x80U | (cp & 0x3FU));
    }
    else if (cp < 0x10000U)
    {
        out += static_cast<char>(0xE0U | (cp >> 12U));
        out += static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
        out += static_cast<char>(0x80U | (cp & 0x3FU));
    }
    else if (cp < 0x110000U)
    {
        out += static_cast<char>(0xF0U | (cp >> 18U));
        out += static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU));
        out += static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
        out += static_cast<char>(0x80U | (cp & 0x3FU));
    }
}

std::string Base64Encode(std::span<const uint8_t> data)
{
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3)
    {
        const uint32_t v = (uint32_t{data[i]} << 16U) | (uint32_t{data[i + 1]} << 8U) | data[i + 2];
        out += kAlphabet[(v >> 18U) & 63U];
        out += kAlphabet[(v >> 12U) & 63U];
        out += kAlphabet[(v >> 6U) & 63U];
        out += kAlphabet[v & 63U];
    }
    const size_t rest = data.size() - i;
    if (rest == 1)
    {
        const uint32_t v = uint32_t{data[i]} << 16U;
        out += kAlphabet[(v >> 18U) & 63U];
        out += kAlphabet[(v >> 12U) & 63U];
        out += "==";
    }
    else if (rest == 2)
    {
        const uint32_t v = (uint32_t{data[i]} << 16U) | (uint32_t{data[i + 1]} << 8U);
        out += kAlphabet[(v >> 18U) & 63U];
        out += kAlphabet[(v >> 12U) & 63U];
        out += kAlphabet[(v >> 6U) & 63U];
        out += '=';
    }
    return out;
}

std::string Base64Encode(std::string_view data)
{
    return Base64Encode(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(data.data()), data.size()));
}

std::vector<uint8_t> Base64Decode(std::string_view text)
{
    std::vector<uint8_t> out;
    out.reserve(text.size() * 3 / 4);
    uint32_t acc  = 0;
    int      bits = 0;
    for (const char c : text)
    {
        if (IsSpace(c))
        {
            continue;
        }
        if (c == '=')
        {
            break;
        }
        const int v = Base64Value(c);
        if (v < 0)
        {
            throw ParseError("invalid base64 character");
        }
        acc = (acc << 6U) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> static_cast<uint32_t>(bits)) & 0xFFU));
        }
    }
    return out;
}

std::string UrlDecode(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size())
        {
            const int hi = HexValue(s[i + 1]);
            const int lo = HexValue(s[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out += static_cast<char>(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

std::string DecodeHtmlEntities(std::string_view s)
{
    static constexpr std::array<std::pair<std::string_view, uint32_t>, 7> kNamed{{
        {"amp", '&'},
        {"lt", '<'},
        {"gt", '>'},
        {"quot", '"'},
        {"apos", '\''},
        {"nbsp", 0xA0},
        {"#39", '\''},
    }};
    std::string                                                           out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != '&')
        {
            out += s[i];
            continue;
        }
        const size_t semi = s.find(';', i + 1);
        if (semi == std::string_view::npos || semi - i > 10)
        {
            out += s[i];
            continue;
        }
        const std::string_view name = s.substr(i + 1, semi - i - 1);
        bool                   done = false;
        for (const auto &[n, cp] : kNamed)
        {
            if (name == n)
            {
                AppendUtf8(out, cp == 0xA0 ? uint32_t{' '} : cp);
                done = true;
                break;
            }
        }
        if (!done && name.size() > 1 && name[0] == '#')
        {
            uint32_t   cp  = 0;
            const bool hex = name[1] == 'x' || name[1] == 'X';
            const auto num = name.substr(hex ? 2 : 1);
            const auto res = std::from_chars(num.data(), num.data() + num.size(), cp, hex ? 16 : 10);
            done           = res.ec == std::errc{} && res.ptr == num.data() + num.size();
            if (done)
            {
                AppendUtf8(out, cp);
            }
        }
        if (!done)
        {
            out += s[i];
            continue;
        }
        i = semi;
    }
    return out;
}

std::string HtmlToText(std::string_view html)
{
    std::string text;
    text.reserve(html.size());
    for (size_t i = 0; i < html.size(); ++i)
    {
        if (html[i] != '<')
        {
            text += html[i];
            continue;
        }
        const size_t close = html.find('>', i);
        if (close == std::string_view::npos)
        {
            text += html.substr(i);
            break;
        }
        std::string tag(html.substr(i + 1, close - i - 1));
        for (char &c : tag)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (tag.starts_with("br") || tag == "/div" || tag == "/p" || tag == "/li")
        {
            text += '\n';
        }
        i = close;
    }
    const std::string decoded = DecodeHtmlEntities(text);
    // collapse whitespace inside lines, trim lines, drop empty ones
    std::string out;
    std::string line;
    auto        flush = [&]
    {
        const std::string t = Trim(line);
        if (!t.empty())
        {
            if (!out.empty())
            {
                out += '\n';
            }
            out += t;
        }
        line.clear();
    };
    bool space = false;
    for (const char c : decoded)
    {
        if (c == '\n')
        {
            flush();
            space = false;
            continue;
        }
        if (IsSpace(c))
        {
            space = true;
            continue;
        }
        if (space && !line.empty())
        {
            line += ' ';
        }
        space = false;
        line += c;
    }
    flush();
    return out;
}

std::string Trim(std::string_view s)
{
    while (!s.empty() && IsSpace(s.front()))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && IsSpace(s.back()))
    {
        s.remove_suffix(1);
    }
    return std::string(s);
}

std::string Slugify(std::string_view s, std::string_view fallback)
{
    std::string out;
    bool        dash = false;
    for (const char c : s)
    {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) != 0 && uc < 0x80U)
        {
            if (dash && !out.empty())
            {
                out += '-';
            }
            out += static_cast<char>(std::tolower(uc));
            dash = false;
        }
        else
        {
            dash = true;
        }
    }
    return out.empty() ? std::string(fallback) : out;
}

} // namespace ad
