#include "Opc.hpp"

#include <zip.h>

#include <pugixml.hpp>

#include <algorithm>
#include <sstream>
#include <utility>

#include "Common/Exceptions.hpp"
#include "Utils/File.hpp"

namespace ad::opc
{

namespace
{

constexpr std::string_view kContentTypes = "[Content_Types].xml";
constexpr std::string_view kRelsNs       = "http://schemas.openxmlformats.org/package/2006/relationships";

std::string ZipError(zip_t *z) { return z != nullptr ? std::string(zip_strerror(z)) : std::string("zip error"); }

/// "ppt/slides/slide1.xml" -> "ppt/slides/_rels/slide1.xml.rels"; "" -> "_rels/.rels"
std::string RelsPart(std::string_view source)
{
    const size_t      slash = source.rfind('/');
    const std::string dir   = slash == std::string_view::npos ? std::string() : std::string(source.substr(0, slash + 1));
    const std::string name  = slash == std::string_view::npos ? std::string(source) : std::string(source.substr(slash + 1));
    return dir + "_rels/" + name + ".rels";
}

std::string Serialize(const pugi::xml_document &doc)
{
    std::ostringstream out;
    doc.save(out, "", pugi::format_raw, pugi::encoding_utf8);
    return out.str();
}

pugi::xml_document ParseXml(const std::string &text, std::string_view part)
{
    pugi::xml_document doc;
    if (const auto r = doc.load_buffer(text.data(), text.size()); !r)
    {
        throw ParseError(std::string(part) + ": " + r.description());
    }
    return doc;
}

std::vector<std::string> SplitPath(std::string_view p)
{
    std::vector<std::string> out;
    size_t                   start = 0;
    while (start <= p.size())
    {
        const size_t end = p.find('/', start);
        out.emplace_back(p.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }
    return out;
}

} // namespace

Package::Package(zip_t *archive, std::filesystem::path path) : _zip(archive), _path(std::move(path)) {}

Package::Package(Package &&other) noexcept
    : _zip(std::exchange(other._zip, nullptr)), _path(std::move(other._path)), _written(std::move(other._written))
{
}

Package &Package::operator=(Package &&other) noexcept
{
    if (this != &other)
    {
        if (_zip != nullptr)
        {
            zip_discard(_zip);
        }
        _zip     = std::exchange(other._zip, nullptr);
        _path    = std::move(other._path);
        _written = std::move(other._written);
    }
    return *this;
}

Package::~Package()
{
    if (_zip != nullptr)
    {
        zip_discard(_zip); // not committed: the file keeps its previous content
    }
}

Package Package::Open(const std::filesystem::path &path)
{
    int    err = 0;
    zip_t *z   = zip_open(PathToUtf8(path).c_str(), 0, &err);
    if (z == nullptr)
    {
        zip_error_t e;
        zip_error_init_with_code(&e, err);
        std::string msg = PathToUtf8(path) + ": " + zip_error_strerror(&e);
        zip_error_fini(&e);
        throw IoError(msg);
    }
    return Package(z, path);
}

Package Package::Create(const std::filesystem::path &path, std::span<const uint8_t> bytes)
{
    ad::WriteFile(path, std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
    return Open(path);
}

void Package::_Check() const
{
    if (_zip == nullptr)
    {
        throw IoError("package is closed");
    }
}

bool Package::Has(std::string_view part) const
{
    _Check();
    return _written.contains(std::string(part)) || zip_name_locate(_zip, std::string(part).c_str(), 0) >= 0;
}

std::string Package::Read(std::string_view part) const
{
    _Check();
    const std::string name(part);
    if (const auto it = _written.find(name); it != _written.end())
    {
        return it->second;
    }
    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat(_zip, name.c_str(), 0, &st) != 0)
    {
        throw IoError("missing part " + name);
    }
    zip_file_t *f = zip_fopen(_zip, name.c_str(), ZIP_FL_UNCHANGED);
    if (f == nullptr)
    {
        throw IoError(name + ": " + ZipError(_zip));
    }
    std::string data(static_cast<size_t>(st.size), '\0');
    const auto  n = zip_fread(f, data.data(), st.size);
    zip_fclose(f);
    if (n < 0 || static_cast<zip_uint64_t>(n) != st.size)
    {
        throw IoError(name + ": read error");
    }
    return data;
}

std::vector<std::string> Package::Parts() const
{
    _Check();
    std::vector<std::string> out;
    const zip_int64_t        n = zip_get_num_entries(_zip, 0);
    for (zip_int64_t i = 0; i < n; ++i)
    {
        if (const char *name = zip_get_name(_zip, static_cast<zip_uint64_t>(i), 0); name != nullptr)
        {
            out.emplace_back(name);
        }
    }
    std::ranges::sort(out);
    return out;
}

void Package::Write(std::string_view part, std::string data)
{
    _Check();
    std::string &buf  = _written[std::string(part)];
    buf               = std::move(data);
    zip_source_t *src = zip_source_buffer(_zip, buf.data(), buf.size(), 0);
    if (src == nullptr)
    {
        throw IoError(std::string(part) + ": " + ZipError(_zip));
    }
    const zip_int64_t idx = zip_file_add(_zip, std::string(part).c_str(), src, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
    if (idx < 0)
    {
        zip_source_free(src);
        throw IoError(std::string(part) + ": " + ZipError(_zip));
    }
    zip_set_file_compression(_zip, static_cast<zip_uint64_t>(idx), ZIP_CM_DEFLATE, 0);
}

void Package::WriteFile(std::string_view part, const std::filesystem::path &file)
{
    _Check();
    zip_source_t *src = zip_source_file(_zip, PathToUtf8(file).c_str(), 0, -1); // -1: to the end
    if (src == nullptr)
    {
        throw IoError(PathToUtf8(file) + ": " + ZipError(_zip));
    }
    const zip_int64_t idx = zip_file_add(_zip, std::string(part).c_str(), src, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
    if (idx < 0)
    {
        zip_source_free(src);
        throw IoError(std::string(part) + ": " + ZipError(_zip));
    }
    zip_set_file_compression(_zip, static_cast<zip_uint64_t>(idx), ZIP_CM_STORE, 0);
}

std::string Package::FreeName(std::string_view prefix, std::string_view suffix) const
{
    for (int n = 1;; ++n)
    {
        std::string name = std::string(prefix) + std::to_string(n) + std::string(suffix);
        if (!Has(name))
        {
            return name;
        }
    }
}

void Package::AddDefaultContentType(std::string_view extension, std::string_view content_type)
{
    auto doc   = ParseXml(Read(kContentTypes), kContentTypes);
    auto types = doc.child("Types");
    for (auto d : types.children("Default"))
    {
        if (std::string_view(d.attribute("Extension").value()) == extension)
        {
            return;
        }
    }
    auto d = types.prepend_child("Default");
    d.append_attribute("Extension").set_value(std::string(extension).c_str());
    d.append_attribute("ContentType").set_value(std::string(content_type).c_str());
    Write(kContentTypes, Serialize(doc));
}

void Package::AddOverrideContentType(std::string_view part, std::string_view content_type)
{
    auto              doc   = ParseXml(Read(kContentTypes), kContentTypes);
    auto              types = doc.child("Types");
    const std::string name  = "/" + std::string(part);
    for (auto o : types.children("Override"))
    {
        if (name == o.attribute("PartName").value())
        {
            o.attribute("ContentType").set_value(std::string(content_type).c_str());
            Write(kContentTypes, Serialize(doc));
            return;
        }
    }
    auto o = types.append_child("Override");
    o.append_attribute("PartName").set_value(name.c_str());
    o.append_attribute("ContentType").set_value(std::string(content_type).c_str());
    Write(kContentTypes, Serialize(doc));
}

std::vector<Relationship> Package::Relationships(std::string_view source_part) const
{
    const std::string rels = RelsPart(source_part);
    if (!Has(rels))
    {
        return {};
    }
    const auto                doc = ParseXml(Read(rels), rels);
    std::vector<Relationship> out;
    for (auto r : doc.child("Relationships").children("Relationship"))
    {
        out.push_back({r.attribute("Id").value(), r.attribute("Type").value(), r.attribute("Target").value(),
                       std::string_view(r.attribute("TargetMode").value()) == "External"});
    }
    return out;
}

std::string Package::AddRelationship(std::string_view source_part, std::string_view type, std::string_view target)
{
    const std::string  rels = RelsPart(source_part);
    pugi::xml_document doc;
    if (Has(rels))
    {
        doc = ParseXml(Read(rels), rels);
    }
    else
    {
        doc.append_child(pugi::node_declaration).append_attribute("version").set_value("1.0");
        doc.child("xml").append_attribute("encoding").set_value("UTF-8");
        doc.child("xml").append_attribute("standalone").set_value("yes");
        doc.append_child("Relationships").append_attribute("xmlns").set_value(std::string(kRelsNs).c_str());
    }
    auto root = doc.child("Relationships");
    int  max  = 0;
    for (auto r : root.children("Relationship"))
    {
        const std::string_view id(r.attribute("Id").value());
        if (id.starts_with("rId"))
        {
            max = std::max(max, std::atoi(std::string(id.substr(3)).c_str()));
        }
    }
    const std::string id = "rId" + std::to_string(max + 1);
    auto              r  = root.append_child("Relationship");
    r.append_attribute("Id").set_value(id.c_str());
    r.append_attribute("Type").set_value(std::string(type).c_str());
    r.append_attribute("Target").set_value(std::string(target).c_str());
    Write(rels, Serialize(doc));
    return id;
}

std::string Package::ResolveTarget(std::string_view source_part, std::string_view target)
{
    if (target.starts_with('/'))
    {
        return std::string(target.substr(1));
    }
    auto parts = SplitPath(source_part);
    parts.pop_back(); // file name of the source
    for (const auto &seg : SplitPath(target))
    {
        if (seg == "..")
        {
            if (!parts.empty())
            {
                parts.pop_back();
            }
        }
        else if (seg != "." && !seg.empty())
        {
            parts.push_back(seg);
        }
    }
    std::string out;
    for (const auto &p : parts)
    {
        out += (out.empty() ? "" : "/") + p;
    }
    return out;
}

std::string Package::RelativeTarget(std::string_view source_part, std::string_view target_part)
{
    auto from = SplitPath(source_part);
    from.pop_back();
    const auto to     = SplitPath(target_part);
    size_t     common = 0;
    while (common < from.size() && common + 1 < to.size() && from[common] == to[common])
    {
        ++common;
    }
    std::string out;
    for (size_t i = common; i < from.size(); ++i)
    {
        out += "../";
    }
    for (size_t i = common; i < to.size(); ++i)
    {
        out += to[i] + (i + 1 < to.size() ? "/" : "");
    }
    return out;
}

void Package::Commit()
{
    _Check();
    if (zip_close(_zip) != 0)
    {
        const std::string msg = ZipError(_zip);
        zip_discard(_zip);
        _zip = nullptr;
        throw IoError(PathToUtf8(_path) + ": " + msg);
    }
    _zip = nullptr;
    _written.clear();
}

} // namespace ad::opc
