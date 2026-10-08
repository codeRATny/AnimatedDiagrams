#include "Registry.hpp"

#include <algorithm>
#include <set>

#include "Model/Model.hpp"

namespace ad
{

Registry::Registry() { _sources.push_back({std::string(kBuiltinSource), BuiltinLibrary()}); }

const Registry &Registry::Default()
{
    static const Registry kDefault;
    return kDefault;
}

void Registry::AddSource(const std::string &source_id, LibrarySet set)
{
    RemoveSource(source_id);
    _sources.push_back({source_id, std::move(set)});
}

void Registry::RemoveSource(std::string_view source_id)
{
    if (source_id == kBuiltinSource)
    {
        return;
    }
    std::erase_if(_sources,
                  [source_id](const NamedSet &s)
                  {
                      return s.id == source_id;
                  });
}

void Registry::ClearPlugins() { _sources.resize(1); }

std::vector<std::string> Registry::Sources() const
{
    std::vector<std::string> out;
    for (const auto &s : _sources)
    {
        out.push_back(s.id);
    }
    return out;
}

const LibrarySet *Registry::Source(std::string_view source_id) const
{
    const auto it = std::ranges::find(_sources, source_id, &NamedSet::id);
    return it != _sources.end() ? &it->set : nullptr;
}

template <class T, class Getter>
const T *Registry::_Find(std::string_view id, const LibrarySet *doc, Getter get) const
{
    if (doc != nullptr)
    {
        if (const T *d = get(*doc, id); d != nullptr)
        {
            return d;
        }
    }
    for (auto it = _sources.rbegin(); it != _sources.rend(); ++it)
    {
        if (const T *d = get(it->set, id); d != nullptr)
        {
            return d;
        }
    }
    return nullptr;
}

const ElementType *Registry::FindElement(std::string_view id, const LibrarySet *doc) const
{
    return _Find<ElementType>(id, doc,
                              [](const LibrarySet &s, std::string_view i)
                              {
                                  return s.Element(i);
                              });
}

const EffectDef *Registry::FindEffect(std::string_view id, const LibrarySet *doc) const
{
    return _Find<EffectDef>(id, doc,
                            [](const LibrarySet &s, std::string_view i)
                            {
                                return s.Effect(i);
                            });
}

const AnimationTemplate *Registry::FindAnimation(std::string_view id, const LibrarySet *doc) const
{
    return _Find<AnimationTemplate>(id, doc,
                                    [](const LibrarySet &s, std::string_view i)
                                    {
                                        return s.Animation(i);
                                    });
}

const ElementType &Registry::Element(std::string_view id, const LibrarySet *doc) const
{
    if (const ElementType *e = FindElement(id, doc); e != nullptr)
    {
        return *e;
    }
    return *_sources.front().set.Element(kDefaultType);
}

template <class T, class Member>
std::vector<RegistryEntry<T>> Registry::_List(const LibrarySet *doc, Member member) const
{
    // walk from the highest priority (document) down, keep the first occurrence of each id,
    // then reverse groups so the listing goes built-in -> plugins -> document
    std::set<std::string>                      seen;
    std::vector<std::vector<RegistryEntry<T>>> groups;
    auto                                       collect = [&](const LibrarySet &set, const std::string &source)
    {
        std::vector<RegistryEntry<T>> group;
        for (const T &def : set.*member)
        {
            if (seen.insert(def.id).second)
            {
                group.push_back({&def, source});
            }
        }
        groups.push_back(std::move(group));
    };
    if (doc != nullptr)
    {
        collect(*doc, std::string(kDocumentSource));
    }
    for (auto it = _sources.rbegin(); it != _sources.rend(); ++it)
    {
        collect(it->set, it->id);
    }
    std::vector<RegistryEntry<T>> out;
    for (auto it = groups.rbegin(); it != groups.rend(); ++it)
    {
        out.insert(out.end(), it->begin(), it->end());
    }
    return out;
}

std::vector<RegistryEntry<ElementType>> Registry::Elements(const LibrarySet *doc) const
{
    return _List<ElementType>(doc, &LibrarySet::elements);
}

std::vector<RegistryEntry<EffectDef>> Registry::Effects(const LibrarySet *doc) const { return _List<EffectDef>(doc, &LibrarySet::effects); }

std::vector<RegistryEntry<AnimationTemplate>> Registry::Animations(const LibrarySet *doc) const
{
    return _List<AnimationTemplate>(doc, &LibrarySet::animations);
}

int Registry::EmbedUsedDefinitions(Model &model) const
{
    const LibrarySet &builtins = _sources.front().set;
    int               copied   = 0;
    for (const auto &n : model.nodes)
    {
        if (model.library.Element(n.type) != nullptr || builtins.Element(n.type) != nullptr)
        {
            continue;
        }
        if (const ElementType *e = FindElement(n.type); e != nullptr)
        {
            model.library.Upsert(*e);
            ++copied;
        }
    }
    for (const auto &s : model.scenario.steps)
    {
        if (s.type != StepType::Effect || model.library.Effect(s.effect) != nullptr || builtins.Effect(s.effect) != nullptr)
        {
            continue;
        }
        if (const EffectDef *e = FindEffect(s.effect); e != nullptr)
        {
            model.library.Upsert(*e);
            ++copied;
        }
    }
    return copied;
}

} // namespace ad
