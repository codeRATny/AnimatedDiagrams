#ifndef _EXPORT_OPC_HPP_
#define _EXPORT_OPC_HPP_

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// @file Opc.hpp
/// @brief Open Packaging Conventions container (the zip of .pptx / .docx / .xlsx) on top of
///        libzip: parts, content types and relationships. Part names have no leading '/'
///        ("ppt/slides/slide1.xml"). Changes are written by Commit().

struct zip;

namespace ad::opc
{

inline constexpr std::string_view kRelOfficeDocument = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument";
inline constexpr std::string_view kRelSlide          = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide";
inline constexpr std::string_view kRelSlideLayout    = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout";
inline constexpr std::string_view kRelImage          = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image";
inline constexpr std::string_view kRelVideo          = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/video";
inline constexpr std::string_view kRelMedia          = "http://schemas.microsoft.com/office/2007/relationships/media";

struct Relationship
{
    std::string id;
    std::string type;
    std::string target; // as written (relative to the source part's folder)
    bool        external = false;
};

class Package
{
public:
    /// Open an existing package for reading and modification. Throws ad::IoError.
    static Package Open(const std::filesystem::path &path);
    /// Write `bytes` (e.g. a template package) to `path` and open it. Throws ad::IoError.
    static Package Create(const std::filesystem::path &path, std::span<const uint8_t> bytes);

    Package(Package &&other) noexcept;
    Package &operator=(Package &&other) noexcept;
    Package(const Package &)            = delete;
    Package &operator=(const Package &) = delete;
    ~Package();

    [[nodiscard]] bool                     Has(std::string_view part) const;
    [[nodiscard]] std::string              Read(std::string_view part) const;
    [[nodiscard]] std::vector<std::string> Parts() const;

    /// Add or replace a part (deflated).
    void Write(std::string_view part, std::string data);
    /// Add or replace a part with the content of a file (stored: media is already compressed).
    void WriteFile(std::string_view part, const std::filesystem::path &file);

    /// "<prefix><n><suffix>" with the smallest n >= 1 that is not used yet.
    [[nodiscard]] std::string FreeName(std::string_view prefix, std::string_view suffix) const;

    // ---- [Content_Types].xml
    void AddDefaultContentType(std::string_view extension, std::string_view content_type);
    void AddOverrideContentType(std::string_view part, std::string_view content_type);

    // ---- relationships of a part ("" -- package relationships)
    [[nodiscard]] std::vector<Relationship> Relationships(std::string_view source_part) const;
    /// Adds a relationship and returns its id ("rIdN").
    std::string AddRelationship(std::string_view source_part, std::string_view type, std::string_view target);
    /// Part name a relationship of `source_part` points to (resolves "../").
    [[nodiscard]] static std::string ResolveTarget(std::string_view source_part, std::string_view target);
    /// Relative reference from `source_part` to `target_part` ("../media/media1.mp4").
    [[nodiscard]] static std::string RelativeTarget(std::string_view source_part, std::string_view target_part);

    /// Write the archive. The package cannot be used afterwards. Throws ad::IoError.
    void Commit();

private:
    explicit Package(::zip *archive, std::filesystem::path path);
    void _Check() const;

    ::zip                             *_zip = nullptr;
    std::filesystem::path              _path;
    std::map<std::string, std::string> _written; // parts added in this session (zip sources point here until Commit())
};

} // namespace ad::opc

#endif // _EXPORT_OPC_HPP_
