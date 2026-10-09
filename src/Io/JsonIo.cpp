#include "JsonIo.hpp"

#include <algorithm>
#include <set>

#include "Io/JsonCodec.hpp"
#include "Model/Document.hpp"
#include "Utils/I18n.hpp"

namespace ad
{

void NormalizeModel(Model &m)
{
    IdGenerator           ids;
    std::set<std::string> seen;
    auto                  unique_id = [&](std::string &id, std::string_view prefix)
    {
        if (id.empty() || seen.contains(id))
        {
            id = ids.Next(prefix, m);
        }
        seen.insert(id);
    };

    m.version = kModelVersion;
    if (m.meta.name.empty())
    {
        m.meta.name = Tr("document", "Untitled");
    }
    if (!(m.view.zoom > 0))
    {
        m.view.zoom = 1;
    }
    m.view.zoom = std::clamp(m.view.zoom, 0.25, 3.0);
    if (!Color::Parse(m.scene.background).has_value())
    {
        m.scene.background = SceneSettings{}.background;
    }
    m.scene.grid_size = std::clamp(m.scene.grid_size, 4.0, 200.0);

    for (auto &n : m.nodes)
    {
        unique_id(n.id, "n");
        if (n.type.empty())
        {
            n.type = kDefaultType;
        }
        if (n.w < 20)
        {
            n.w = kDefaultNodeW;
        }
        if (n.h < 20)
        {
            n.h = kDefaultNodeH;
        }
        if (!Color::Parse(n.accent).has_value())
        {
            n.accent.clear();
        }
        for (auto &p : n.ports)
        {
            unique_id(p.id, "p");
        }
    }

    std::erase_if(m.edges,
                  [&](const Edge &e)
                  {
                      return e.from == e.to || m.FindNode(e.from) == nullptr || m.FindNode(e.to) == nullptr;
                  });
    for (auto &e : m.edges)
    {
        unique_id(e.id, "e");
        if (m.FindPort(e.from, e.from_port) == nullptr)
        {
            e.from_port.clear();
        }
        if (m.FindPort(e.to, e.to_port) == nullptr)
        {
            e.to_port.clear();
        }
    }

    auto &steps = m.scenario.steps;
    std::erase_if(steps,
                  [&](const Step &s)
                  {
                      switch (s.type)
                      {
                      case StepType::Message:
                          return m.FindNode(s.from) == nullptr || m.FindNode(s.to) == nullptr;
                      case StepType::Link:
                          return m.FindEdge(s.edge_id) == nullptr;
                      case StepType::Note:
                          return false;
                      default:
                          return m.FindNode(s.node_id) == nullptr;
                      }
                  });
    for (auto &s : steps)
    {
        unique_id(s.id, "s");
        s.start        = std::max(0.0, s.start);
        s.duration     = std::max(1.0, s.duration);
        s.packet_count = std::clamp(s.packet_count, 1, 10);
        s.packet_size  = std::clamp(s.packet_size, 0.2, 5.0);
        if (m.FindEdge(s.edge_id) == nullptr)
        {
            s.edge_id.clear();
        }
    }
    std::ranges::stable_sort(steps, {}, &Step::start);

    if (!(m.scenario.duration >= 1000))
    {
        m.scenario.duration = AutoDuration(m.scenario);
    }
}

std::expected<Model, std::string> ParseModel(std::string_view text)
{
    const json::Json j = json::Json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
    {
        return std::unexpected(std::string(Tr("document", "The file is not valid JSON")));
    }
    if (!j.is_object() || !j.contains("nodes") || !j.contains("scenario"))
    {
        return std::unexpected(std::string(Tr("document", "Invalid file format: no nodes/scenario")));
    }
    Model m = json::ModelFromJson(j);
    NormalizeModel(m);
    return m;
}

std::string SerializeModel(const Model &m, int indent)
{
    return json::ToJson(m).dump(indent, ' ', false, json::OrderedJson::error_handler_t::replace);
}

} // namespace ad
