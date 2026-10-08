#include "Templates.hpp"

#include <algorithm>
#include <limits>

#include "Common/Exceptions.hpp"

namespace ad
{

std::vector<std::string> ApplyTemplate(Document &doc, const AnimationTemplate &tpl, const std::map<std::string, std::string> &roles,
                                       double start)
{
    const Model &m = doc.Get();
    for (const auto &role : tpl.roles)
    {
        const auto it = roles.find(role.id);
        if (it == roles.end() || m.FindNode(it->second) == nullptr)
        {
            throw InvalidArgument("role '" + role.id + "' is not mapped to an existing node");
        }
    }
    auto map_role = [&](const std::string &value)
    {
        const auto it = roles.find(value);
        return it != roles.end() ? it->second : value;
    };

    std::vector<Step> steps;
    for (Step s : tpl.steps)
    {
        s.from    = map_role(s.from);
        s.to      = map_role(s.to);
        s.node_id = map_role(s.node_id);
        s.start   = std::max(0.0, start + s.start);
        s.id.clear();
        if (s.type == StepType::Message)
        {
            s.edge_id.clear();
            if (const Edge *e = m.EdgeBetween(s.from, s.to); e != nullptr)
            {
                s.edge_id = e->id;
            }
        }
        else if (s.type == StepType::Link)
        {
            const auto sep = s.edge_id.find('>');
            if (sep == std::string::npos)
            {
                continue;
            }
            const Edge *e = m.EdgeBetween(map_role(s.edge_id.substr(0, sep)), map_role(s.edge_id.substr(sep + 1)));
            if (e == nullptr)
            {
                continue;
            }
            s.edge_id = e->id;
        }
        steps.push_back(std::move(s));
    }

    doc.Checkpoint();
    std::vector<std::string> ids;
    Model                   &mm = doc.Mutable();
    for (Step &s : steps)
    {
        s.id = doc.NewId("s");
        ids.push_back(s.id);
        mm.scenario.steps.push_back(std::move(s));
    }
    doc.SortSteps();
    doc.UpdateDuration();
    return ids;
}

AnimationTemplate MakeTemplate(const Model &m, const std::vector<std::string> &step_ids, const std::string &id, const std::string &label)
{
    AnimationTemplate tpl;
    tpl.id    = id;
    tpl.label = label;

    std::vector<const Step *> src;
    for (const auto &sid : step_ids)
    {
        if (const Step *s = m.FindStep(sid); s != nullptr)
        {
            src.push_back(s);
        }
    }
    std::ranges::stable_sort(src, {}, &Step::start);
    double t0 = std::numeric_limits<double>::infinity();
    for (const Step *s : src)
    {
        t0 = std::min(t0, s->start);
    }

    std::map<std::string, std::string> role_of; // node id -> role id
    auto                               role = [&](const std::string &node_id) -> std::string
    {
        if (node_id.empty() || m.FindNode(node_id) == nullptr)
        {
            return node_id;
        }
        if (const auto it = role_of.find(node_id); it != role_of.end())
        {
            return it->second;
        }
        const std::string rid = "role" + std::to_string(role_of.size() + 1);
        role_of[node_id]      = rid;
        tpl.roles.push_back({rid, m.FindNode(node_id)->label});
        return rid;
    };

    int n = 0;
    for (const Step *orig : src)
    {
        Step s    = *orig;
        s.id      = "t" + std::to_string(++n);
        s.start   = s.start - t0;
        s.from    = role(s.from);
        s.to      = role(s.to);
        s.node_id = role(s.node_id);
        if (s.type == StepType::Link)
        {
            const Edge *e = m.FindEdge(s.edge_id);
            if (e == nullptr)
            {
                continue;
            }
            // sequenced: role ids are numbered in order of appearance
            const std::string from_role = role(e->from);
            const std::string to_role   = role(e->to);
            s.edge_id                   = from_role + ">" + to_role;
        }
        else
        {
            s.edge_id.clear();
        }
        tpl.steps.push_back(std::move(s));
    }
    return tpl;
}

std::vector<std::string> StepsInRange(const Model &m, double from, double to)
{
    std::vector<std::string> out;
    for (const auto &s : m.scenario.steps)
    {
        if (s.start >= from && s.start <= to)
        {
            out.push_back(s.id);
        }
    }
    return out;
}

} // namespace ad
