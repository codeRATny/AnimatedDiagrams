#include "Plugin.hpp"

#include <algorithm>
#include <cctype>

#include "Io/JsonCodec.hpp"

namespace ad
{

bool IsValidPluginId(std::string_view id)
{
    if (id.empty() || id.size() > 128)
    {
        return false;
    }
    auto ok = [](char c, bool first)
    {
        const auto uc = static_cast<unsigned char>(c);
        if (std::islower(uc) != 0 || std::isdigit(uc) != 0)
        {
            return uc < 0x80U;
        }
        return !first && (c == '.' || c == '_' || c == '-');
    };
    for (size_t i = 0; i < id.size(); ++i)
    {
        if (!ok(id[i], i == 0))
        {
            return false;
        }
    }
    return true;
}

std::expected<Plugin, std::string> ParsePlugin(std::string_view json_text, std::vector<std::string> *warnings)
{
    const json::Json j = json::Json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object())
    {
        return std::unexpected(std::string("plugin manifest is not a JSON object"));
    }
    const auto format = j.value("format", std::string{});
    if (format != kPluginFormat)
    {
        return std::unexpected("not a plugin manifest (\"format\" must be \"" + std::string(kPluginFormat) + "\")");
    }
    const int version = j.value("formatVersion", 1);
    if (version > kPluginFormatVersion)
    {
        return std::unexpected("plugin format version " + std::to_string(version) + " is newer than supported (" +
                               std::to_string(kPluginFormatVersion) + ")");
    }
    Plugin p;
    auto   str = [&](const char *key)
    {
        const auto it = j.find(key);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
    };
    p.info.id          = str("id");
    p.info.name        = str("name");
    p.info.version     = str("version");
    p.info.author      = str("author");
    p.info.description = str("description");
    p.info.homepage    = str("homepage");
    if (!IsValidPluginId(p.info.id))
    {
        return std::unexpected("invalid plugin id '" + p.info.id + "' (allowed: a-z 0-9 . _ -)");
    }
    if (p.info.name.empty())
    {
        p.info.name = p.info.id;
    }
    if (p.info.version.empty())
    {
        p.info.version = "1.0.0";
    }
    p.library = json::LibraryFromJson(j, warnings);
    if (const auto it = j.find("translations"); it != j.end() && it->is_object())
    {
        for (const auto &[lang, dict] : it->items())
        {
            if (!dict.is_object())
            {
                continue;
            }
            for (const auto &[source, text] : dict.items())
            {
                if (text.is_string())
                {
                    p.translations[lang][source] = text.get<std::string>();
                }
            }
        }
    }
    if (p.library.Empty() && warnings != nullptr)
    {
        warnings->push_back("plugin '" + p.info.id + "' contains no definitions");
    }
    return p;
}

std::string SerializePlugin(const Plugin &plugin, int indent)
{
    json::OrderedJson j;
    j["format"]        = std::string(kPluginFormat);
    j["formatVersion"] = kPluginFormatVersion;
    j["id"]            = plugin.info.id;
    j["name"]          = plugin.info.name;
    j["version"]       = plugin.info.version;
    if (!plugin.info.author.empty())
    {
        j["author"] = plugin.info.author;
    }
    if (!plugin.info.description.empty())
    {
        j["description"] = plugin.info.description;
    }
    if (!plugin.info.homepage.empty())
    {
        j["homepage"] = plugin.info.homepage;
    }
    const json::OrderedJson library = json::ToJson(plugin.library); // named: items() refers to it
    for (const auto &[key, value] : library.items())
    {
        j[key] = value;
    }
    if (!plugin.translations.empty())
    {
        j["translations"] = plugin.translations;
    }
    return j.dump(indent, ' ', false, json::OrderedJson::error_handler_t::replace);
}

void LocalizePlugin(Plugin &plugin, std::string_view language)
{
    const auto it = plugin.translations.find(std::string(language));
    if (it == plugin.translations.end() || it->second.empty())
    {
        return;
    }
    const auto &dict = it->second;
    auto        tr   = [&dict](std::string &text)
    {
        if (const auto t = dict.find(text); t != dict.end())
        {
            text = t->second;
        }
    };
    auto steps = [&tr](std::vector<Step> &list)
    {
        for (auto &s : list)
        {
            tr(s.label);
            tr(s.text);
        }
    };
    tr(plugin.info.name);
    tr(plugin.info.description);
    auto &lib = plugin.library;
    for (auto &e : lib.elements)
    {
        tr(e.label);
        tr(e.category);
        tr(e.description);
    }
    for (auto &e : lib.effects)
    {
        tr(e.label);
        tr(e.category);
        tr(e.description);
    }
    for (auto &a : lib.animations)
    {
        tr(a.label);
        tr(a.category);
        tr(a.description);
        for (auto &r : a.roles)
        {
            tr(r.label);
        }
        steps(a.steps);
    }
    for (auto &d : lib.design_systems)
    {
        tr(d.label);
        tr(d.category);
        tr(d.description);
    }
}

Plugin MakePlugin(const PluginInfo &info, const LibrarySet &source, const std::vector<std::string> &ids)
{
    Plugin p;
    p.info = info;
    for (const auto &id : ids)
    {
        if (const ElementType *e = source.Element(id); e != nullptr)
        {
            p.library.Upsert(*e);
        }
        if (const EffectDef *e = source.Effect(id); e != nullptr)
        {
            p.library.Upsert(*e);
        }
        if (const AnimationTemplate *a = source.Animation(id); a != nullptr)
        {
            p.library.Upsert(*a);
        }
        if (const DesignSystem *d = source.Design(id); d != nullptr)
        {
            p.library.Upsert(*d);
        }
    }
    return p;
}

} // namespace ad
