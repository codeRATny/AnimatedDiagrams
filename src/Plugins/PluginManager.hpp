#ifndef _PLUGINS_PLUGIN_MANAGER_HPP_
#define _PLUGINS_PLUGIN_MANAGER_HPP_

#include <expected>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "Model/Registry.hpp"
#include "Plugins/Plugin.hpp"

/// @file PluginManager.hpp
/// @brief Discovers plugins in search directories, tracks enabled state, installs / removes them.
///
/// A search directory may contain plugin manifests directly (`*.json`) or
/// sub-directories with a `plugin.json` inside.

namespace ad
{

struct PluginRecord
{
    Plugin                   plugin;
    std::filesystem::path    path;
    bool                     enabled  = true;
    bool                     writable = false; // lives in the user directory (can be uninstalled)
    std::vector<std::string> warnings;
};

struct PluginLoadError
{
    std::filesystem::path path;
    std::string           message;
};

class PluginManager
{
public:
    /// `user_dir` receives installed plugins; `system_dirs` are read-only (bundled plugins).
    void SetDirectories(std::filesystem::path user_dir, std::vector<std::filesystem::path> system_dirs);
    [[nodiscard]] const std::filesystem::path &UserDir() const { return _user_dir; }

    /// Re-read all directories. Returns the number of loaded plugins.
    size_t Scan();

    [[nodiscard]] const std::vector<PluginRecord>    &Plugins() const { return _plugins; }
    [[nodiscard]] const std::vector<PluginLoadError> &Errors() const { return _errors; }
    [[nodiscard]] const PluginRecord                 *Find(std::string_view id) const;

    void                                SetEnabled(std::string_view id, bool enabled);
    void                                SetDisabledIds(std::set<std::string> ids);
    [[nodiscard]] std::set<std::string> DisabledIds() const { return _disabled; }

    /// Validate a manifest and copy it into the user directory as `<id>.json`
    /// (replacing an older version). Returns the installed path.
    std::expected<std::filesystem::path, std::string> Install(const std::filesystem::path &manifest);
    /// Install from memory (e.g. a plugin created in the editor).
    std::expected<std::filesystem::path, std::string> Install(const Plugin &plugin);
    /// Delete a plugin from the user directory. False for bundled plugins.
    bool Uninstall(std::string_view id);

    /// Replace the plugin sources of the registry with the enabled plugins.
    void ApplyTo(Registry &registry) const;

private:
    void _LoadFile(const std::filesystem::path &path, bool writable);

    std::filesystem::path              _user_dir;
    std::vector<std::filesystem::path> _system_dirs;
    std::vector<PluginRecord>          _plugins;
    std::vector<PluginLoadError>       _errors;
    std::set<std::string>              _disabled;
};

} // namespace ad

#endif // _PLUGINS_PLUGIN_MANAGER_HPP_
