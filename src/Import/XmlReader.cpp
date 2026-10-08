#include "XmlReader.hpp"

#include <algorithm>
#include <cctype>

#include "Common/Exceptions.hpp"
#include "Utils/Text.hpp"

namespace ad
{

const std::string *XmlNode::Attr(std::string_view key) const
{
    const auto it = std::ranges::find_if(attributes,
                                         [key](const auto &a)
                                         {
                                             return a.first == key;
                                         });
    return it != attributes.end() ? &it->second : nullptr;
}

std::string XmlNode::AttrOr(std::string_view key, std::string_view def) const
{
    const std::string *v = Attr(key);
    return v != nullptr ? *v : std::string(def);
}

const XmlNode *XmlNode::Child(std::string_view child_name) const
{
    const auto it = std::ranges::find(children, child_name, &XmlNode::name);
    return it != children.end() ? &*it : nullptr;
}

std::vector<const XmlNode *> XmlNode::ChildrenNamed(std::string_view child_name) const
{
    std::vector<const XmlNode *> out;
    for (const auto &c : children)
    {
        if (c.name == child_name)
        {
            out.push_back(&c);
        }
    }
    return out;
}

namespace
{

constexpr int kMaxDepth = 256;

class XmlParser
{
public:
    explicit XmlParser(std::string_view s) : _s(s) {}

    XmlNode Parse()
    {
        _SkipProlog();
        if (_pos >= _s.size() || _s[_pos] != '<')
        {
            throw ParseError("XML: root element expected");
        }
        XmlNode root = _Element(0);
        _SkipMisc();
        if (_pos < _s.size())
        {
            throw ParseError("XML: unexpected content after the root element");
        }
        return root;
    }

private:
    bool _StartsWith(std::string_view p) const { return _s.substr(_pos, p.size()) == p; }

    void _SkipSpace()
    {
        while (_pos < _s.size() && std::isspace(static_cast<unsigned char>(_s[_pos])) != 0)
        {
            ++_pos;
        }
    }

    void _SkipUntil(std::string_view end)
    {
        const size_t p = _s.find(end, _pos);
        if (p == std::string_view::npos)
        {
            throw ParseError("XML: unterminated construct, expected '" + std::string(end) + "'");
        }
        _pos = p + end.size();
    }

    /// Skip whitespace, comments, processing instructions and DOCTYPE.
    void _SkipMisc()
    {
        while (true)
        {
            _SkipSpace();
            if (_StartsWith("<?"))
            {
                _SkipUntil("?>");
            }
            else if (_StartsWith("<!--"))
            {
                _SkipUntil("-->");
            }
            else if (_StartsWith("<!DOCTYPE") || _StartsWith("<!doctype"))
            {
                _SkipUntil(">");
            }
            else
            {
                return;
            }
        }
    }

    void _SkipProlog()
    {
        if (_StartsWith("\xEF\xBB\xBF")) // UTF-8 BOM
        {
            _pos += 3;
        }
        _SkipMisc();
    }

    std::string _Name()
    {
        const size_t start = _pos;
        while (_pos < _s.size())
        {
            const char c = _s[_pos];
            if (std::isspace(static_cast<unsigned char>(c)) != 0 || c == '>' || c == '/' || c == '=')
            {
                break;
            }
            ++_pos;
        }
        if (_pos == start)
        {
            throw ParseError("XML: name expected at offset " + std::to_string(_pos));
        }
        return std::string(_s.substr(start, _pos - start));
    }

    XmlNode _Element(int depth)
    {
        if (depth > kMaxDepth)
        {
            throw ParseError("XML: nesting too deep");
        }
        ++_pos; // '<'
        XmlNode node;
        node.name = _Name();
        while (true)
        {
            _SkipSpace();
            if (_pos >= _s.size())
            {
                throw ParseError("XML: unterminated start tag <" + node.name + ">");
            }
            if (_StartsWith("/>"))
            {
                _pos += 2;
                return node;
            }
            if (_s[_pos] == '>')
            {
                ++_pos;
                break;
            }
            std::string key = _Name();
            _SkipSpace();
            if (_pos >= _s.size() || _s[_pos] != '=')
            {
                throw ParseError("XML: '=' expected after attribute " + key);
            }
            ++_pos;
            _SkipSpace();
            if (_pos >= _s.size() || (_s[_pos] != '"' && _s[_pos] != '\''))
            {
                throw ParseError("XML: quoted value expected for attribute " + key);
            }
            const char   quote = _s[_pos++];
            const size_t end   = _s.find(quote, _pos);
            if (end == std::string_view::npos)
            {
                throw ParseError("XML: unterminated attribute value");
            }
            node.attributes.emplace_back(std::move(key), DecodeHtmlEntities(_s.substr(_pos, end - _pos)));
            _pos = end + 1;
        }
        // content
        while (true)
        {
            if (_pos >= _s.size())
            {
                throw ParseError("XML: missing </" + node.name + ">");
            }
            if (_StartsWith("</"))
            {
                _pos += 2;
                const std::string closing = _Name();
                if (closing != node.name)
                {
                    throw ParseError("XML: </" + closing + "> does not match <" + node.name + ">");
                }
                _SkipSpace();
                if (_pos >= _s.size() || _s[_pos] != '>')
                {
                    throw ParseError("XML: '>' expected");
                }
                ++_pos;
                return node;
            }
            if (_StartsWith("<!--"))
            {
                _SkipUntil("-->");
            }
            else if (_StartsWith("<![CDATA["))
            {
                _pos += 9;
                const size_t end = _s.find("]]>", _pos);
                if (end == std::string_view::npos)
                {
                    throw ParseError("XML: unterminated CDATA");
                }
                node.text += _s.substr(_pos, end - _pos);
                _pos = end + 3;
            }
            else if (_StartsWith("<?"))
            {
                _SkipUntil("?>");
            }
            else if (_s[_pos] == '<')
            {
                node.children.push_back(_Element(depth + 1));
            }
            else
            {
                const size_t end = std::min(_s.find('<', _pos), _s.size());
                node.text += DecodeHtmlEntities(_s.substr(_pos, end - _pos));
                _pos = end;
            }
        }
    }

    std::string_view _s;
    size_t           _pos = 0;
};

} // namespace

XmlNode ParseXml(std::string_view text) { return XmlParser(text).Parse(); }

} // namespace ad
