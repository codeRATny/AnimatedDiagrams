#include "PluginManager.hpp"

#include <algorithm>
#include <system_error>

#include "Common/Exceptions.hpp"
#include "Utils/File.hpp"
#include "Utils/I18n.hpp"

namespace ad
{

namespace fs = std::filesystem;

void PluginManager::SetDirectories(fs::path user_dir, std::vector<fs::path> system_dirs)
{
    _user_dir    = std::move(user_dir);
    _system_dirs = std::move(system_dirs);
}

void PluginManager::_LoadFile(const fs::path &path, bool writable)
{
    std::string text;
    try
    {
        text = ReadFile(path);
    }
    catch (const IoError &e)
    {
        _errors.push_back({path, e.what()});
        return;
    }
    PluginRecord rec;
    auto         parsed = ParsePlugin(text, &rec.warnings);
    if (!parsed.has_value())
    {
        _errors.push_back({path, parsed.error()});
        return;
    }
    if (Find(parsed->info.id) != nullptr)
    {
        _errors.push_back({path, "duplicate plugin id '" + parsed->info.id + "' (already loaded)"});
        return;
    }
    LocalizePlugin(*parsed, UiLanguage()); // texts in the UI language when the plugin has them
    rec.plugin   = std::move(*parsed);
    rec.path     = path;
    rec.writable = writable;
    rec.enabled  = !_disabled.contains(rec.plugin.info.id);
    _plugins.push_back(std::move(rec));
}

size_t PluginManager::Scan()
{
    _plugins.clear();
    _errors.clear();
    // the user directory first: an installed copy overrides a bundled plugin with the same id
    std::vector<std::pair<fs::path, bool>> dirs;
    if (!_user_dir.empty())
    {
        dirs.emplace_back(_user_dir, true);
    }
    for (const auto &d : _system_dirs)
    {
        dirs.emplace_back(d, false);
    }
    for (const auto &[dir, writable] : dirs)
    {
        std::error_code ec;
        if (!fs::is_directory(dir, ec))
        {
            continue;
        }
        std::vector<fs::path> files;
        for (const auto &entry : fs::directory_iterator(dir, ec))
        {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".json")
            {
                files.push_back(entry.path());
            }
            else if (entry.is_directory(ec) && fs::is_regular_file(entry.path() / "plugin.json", ec))
            {
                files.push_back(entry.path() / "plugin.json");
            }
        }
        std::ranges::sort(files);
        for (const auto &f : files)
        {
            _LoadFile(f, writable);
        }
    }
    return _plugins.size();
}

const PluginRecord *PluginManager::Find(std::string_view id) const
{
    const auto it = std::ranges::find_if(_plugins,
                                         [id](const PluginRecord &r)
                                         {
                                             return r.plugin.info.id == id;
                                         });
    return it != _plugins.end() ? &*it : nullptr;
}

void PluginManager::SetEnabled(std::string_view id, bool enabled)
{
    if (enabled)
    {
        _disabled.erase(std::string(id));
    }
    else
    {
        _disabled.insert(std::string(id));
    }
    for (auto &r : _plugins)
    {
        if (r.plugin.info.id == id)
        {
            r.enabled = enabled;
        }
    }
}

void PluginManager::SetDisabledIds(std::set<std::string> ids)
{
    _disabled = std::move(ids);
    for (auto &r : _plugins)
    {
        r.enabled = !_disabled.contains(r.plugin.info.id);
    }
}

std::expected<fs::path, std::string> PluginManager::Install(const Plugin &plugin)
{
    if (_user_dir.empty())
    {
        return std::unexpected(std::string("user plugin directory is not configured"));
    }
    if (!IsValidPluginId(plugin.info.id))
    {
        return std::unexpected("invalid plugin id '" + plugin.info.id + "'");
    }
    const fs::path target = _user_dir / PathFromUtf8(plugin.info.id + ".json");
    try
    {
        WriteFile(target, SerializePlugin(plugin));
    }
    catch (const IoError &e)
    {
        return std::unexpected(std::string(e.what()));
    }
    Scan();
    return target;
}

std::expected<fs::path, std::string> PluginManager::Install(const fs::path &manifest)
{
    std::string text;
    try
    {
        text = ReadFile(manifest);
    }
    catch (const IoError &e)
    {
        return std::unexpected(std::string(e.what()));
    }
    auto parsed = ParsePlugin(text);
    if (!parsed.has_value())
    {
        return std::unexpected(parsed.error());
    }
    return Install(*parsed);
}

bool PluginManager::Uninstall(std::string_view id)
{
    const PluginRecord *rec = Find(id);
    if (rec == nullptr || !rec->writable)
    {
        return false;
    }
    std::error_code ec;
    fs::remove(rec->path, ec);
    if (ec)
    {
        return false;
    }
    Scan();
    return true;
}

void PluginManager::ApplyTo(Registry &registry) const
{
    registry.ClearPlugins();
    // reverse order: plugins scanned first (user directory) are added last and win lookups
    for (auto it = _plugins.rbegin(); it != _plugins.rend(); ++it)
    {
        if (it->enabled)
        {
            registry.AddSource(it->plugin.info.id, it->plugin.library);
        }
    }
}

} // namespace ad
