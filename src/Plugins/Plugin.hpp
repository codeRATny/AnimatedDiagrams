#ifndef _PLUGINS_PLUGIN_HPP_
#define _PLUGINS_PLUGIN_HPP_

#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Model/Library.hpp"

/// @file Plugin.hpp
/// @brief Plugin packages: a JSON manifest with element types, effects and animation templates.
///
/// Format (see docs/plugins.md):
/// @code
/// {
///   "format": "animated-diagrams-plugin", "formatVersion": 1,
///   "id": "com.example.network", "name": "Network pack", "version": "1.0.0",
///   "author": "...", "description": "...",
///   "elements": [...], "effects": [...], "animations": [...], "designSystems": [...],
///   "translations": {"ru": {"Message broker": "Брокер сообщений", ...}}
/// }
/// @endcode
/// Plugins are data only (no native code), so they are portable and safe to share.

namespace ad
{

inline constexpr std::string_view kPluginFormat        = "animated-diagrams-plugin";
inline constexpr int              kPluginFormatVersion = 1;

struct PluginInfo
{
    std::string id; // reverse-DNS style: [a-z0-9][a-z0-9._-]*
    std::string name;
    std::string version = "1.0.0";
    std::string author;
    std::string description;
    std::string homepage;
    friend bool operator==(const PluginInfo &, const PluginInfo &) = default;
};

/// language code -> (source text -> translation)
using PluginTranslations = std::map<std::string, std::map<std::string, std::string>>;

struct Plugin
{
    PluginInfo         info;
    LibrarySet         library;
    PluginTranslations translations;
    friend bool        operator==(const Plugin &, const Plugin &) = default;
};

bool IsValidPluginId(std::string_view id);

/// Parse a manifest. Invalid definitions are skipped and reported in `warnings`;
/// an error is returned for non-JSON input, a wrong format marker or an invalid id.
std::expected<Plugin, std::string> ParsePlugin(std::string_view json_text, std::vector<std::string> *warnings = nullptr);

std::string SerializePlugin(const Plugin &plugin, int indent = 2);

/// Replace user-visible texts (plugin name / description, labels, categories, descriptions,
/// role labels, step labels and texts) with their translations for `language`, when the
/// plugin has them; untranslated texts are kept.
void LocalizePlugin(Plugin &plugin, std::string_view language);

/// Select definitions by id from `source` into a new plugin (unknown ids are ignored).
Plugin MakePlugin(const PluginInfo &info, const LibrarySet &source, const std::vector<std::string> &ids);

} // namespace ad

#endif // _PLUGINS_PLUGIN_HPP_
