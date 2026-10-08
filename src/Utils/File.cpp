#include "File.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

#include "Common/Exceptions.hpp"

namespace ad
{

std::filesystem::path PathFromUtf8(std::string_view utf8)
{
    const std::u8string u8(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size());
    return {u8};
}

std::string PathToUtf8(const std::filesystem::path &p)
{
    const std::u8string u8 = p.u8string();
    return {reinterpret_cast<const char *>(u8.data()), u8.size()};
}

std::string ReadFile(const std::filesystem::path &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        throw IoError("cannot open " + PathToUtf8(path));
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    if (f.bad())
    {
        throw IoError("cannot read " + PathToUtf8(path));
    }
    return ss.str();
}

void WriteFile(const std::filesystem::path &path, std::string_view data)
{
    std::error_code ec;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            throw IoError("cannot write " + PathToUtf8(path));
        }
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!f)
        {
            throw IoError("write failed: " + PathToUtf8(path));
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        std::filesystem::remove(tmp, ec);
        throw IoError("cannot replace " + PathToUtf8(path));
    }
}

} // namespace ad
