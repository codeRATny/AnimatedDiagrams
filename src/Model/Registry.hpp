#ifndef _MODEL_REGISTRY_HPP_
#define _MODEL_REGISTRY_HPP_

#include <string>
#include <string_view>
#include <vector>

#include "Model/Library.hpp"

/// @file Registry.hpp
/// @brief Resolves element types, effects and animation templates by id.
///
/// Lookup order: the document library, then plugin sets (the most recently
/// added first), then the built-in set. Unknown element types resolve to the
/// built-in "service" type so documents always render.

namespace ad
{

struct Model;

inline constexpr std::string_view kBuiltinSource  = "builtin";
inline constexpr std::string_view kDocumentSource = "document";

template <class T>
struct RegistryEntry
{
    const T    *def = nullptr;
    std::string source; // "builtin" | "document" | plugin id
};

class Registry
{
public:
    /// Registry with the built-in set only.
    Registry();

    /// Add (or replace) a named set, e.g. a plugin.
    void AddSource(const std::string &source_id, LibrarySet set);
    void RemoveSource(std::string_view source_id);
    /// Remove every source except the built-in one.
    void                                   ClearPlugins();
    [[nodiscard]] std::vector<std::string> Sources() const;
    [[nodiscard]] const LibrarySet        *Source(std::string_view source_id) const;

    [[nodiscard]] const ElementType       *FindElement(std::string_view id, const LibrarySet *doc = nullptr) const;
    [[nodiscard]] const EffectDef         *FindEffect(std::string_view id, const LibrarySet *doc = nullptr) const;
    [[nodiscard]] const AnimationTemplate *FindAnimation(std::string_view id, const LibrarySet *doc = nullptr) const;
    /// Never null: falls back to the built-in "service" element type.
    [[nodiscard]] const ElementType &Element(std::string_view id, const LibrarySet *doc = nullptr) const;

    /// All visible definitions (an id appears once, with the source that wins the lookup),
    /// ordered built-in -> plugins -> document.
    [[nodiscard]] std::vector<RegistryEntry<ElementType>>       Elements(const LibrarySet *doc = nullptr) const;
    [[nodiscard]] std::vector<RegistryEntry<EffectDef>>         Effects(const LibrarySet *doc = nullptr) const;
    [[nodiscard]] std::vector<RegistryEntry<AnimationTemplate>> Animations(const LibrarySet *doc = nullptr) const;

    /// Copy non-built-in definitions referenced by the model (node types, effects)
    /// into its library so the document renders the same without the plugins.
    /// Returns the number of copied definitions.
    int EmbedUsedDefinitions(Model &model) const;

    /// Shared immutable registry with the built-ins only (defaults for tests and CLI).
    static const Registry &Default();

private:
    struct NamedSet
    {
        std::string id;
        LibrarySet  set;
    };

    template <class T, class Getter>
    const T *_Find(std::string_view id, const LibrarySet *doc, Getter get) const;
    template <class T, class Member>
    std::vector<RegistryEntry<T>> _List(const LibrarySet *doc, Member member) const;

    std::vector<NamedSet> _sources; // [0] -- built-ins
};

} // namespace ad

#endif // _MODEL_REGISTRY_HPP_
