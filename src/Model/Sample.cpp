#include "Sample.hpp"

#include <algorithm>

namespace ad
{

namespace
{

Node Service(std::string id, std::string label, double x, double y)
{
    Node n;
    n.id    = std::move(id);
    n.label = std::move(label);
    n.type  = "service";
    n.x     = x;
    n.y     = y;
    n.w     = 150;
    n.h     = 66;
    return n;
}

Step MakeStep(std::string id, StepType type, double start, double duration)
{
    Step s;
    s.id       = std::move(id);
    s.type     = type;
    s.start    = start;
    s.duration = duration;
    return s;
}

Step Message(std::string id, std::string from, std::string to, std::string variant, std::string label, double start, double duration)
{
    Step s    = MakeStep(std::move(id), StepType::Message, start, duration);
    s.from    = std::move(from);
    s.to      = std::move(to);
    s.variant = std::move(variant);
    s.label   = std::move(label);
    return s;
}

Step OnNode(std::string id, StepType type, std::string node, double start, double duration)
{
    Step s    = MakeStep(std::move(id), type, start, duration);
    s.node_id = std::move(node);
    return s;
}

Step State(std::string id, std::string node, std::string state, double start, double duration)
{
    Step s  = OnNode(std::move(id), StepType::State, std::move(node), start, duration);
    s.state = std::move(state);
    return s;
}

Step Note(std::string id, std::string text, double x, double y, double start, double duration)
{
    Step s = MakeStep(std::move(id), StepType::Note, start, duration);
    s.text = std::move(text);
    s.x    = x;
    s.y    = y;
    return s;
}

} // namespace

Model EmptyModel() { return Model{}; }

Model SampleModel()
{
    Model m;
    m.meta.name = "Пример: fallback с ретраями и таймаутом";
    m.nodes     = {Service("svcA", "Сервис A", 80, 120), Service("svcB", "Сервис B", 480, 60), Service("svcC", "Сервис C", 480, 260)};

    Edge ab;
    ab.id    = "eAB";
    ab.from  = "svcA";
    ab.to    = "svcB";
    ab.label = "REST";
    ab.curve = 0.15;
    Edge ac;
    ac.id                 = "eAC";
    ac.from               = "svcA";
    ac.to                 = "svcC";
    ac.label              = "fallback";
    ac.style.stroke_style = "dashed";
    ac.curve              = -0.15;
    m.edges               = {ab, ac};

    Step timer    = OnNode("s3", StepType::Timer, "svcA", 1600, 5000);
    timer.seconds = 5;
    timer.label   = "timeout";

    Step pulse   = OnNode("s7", StepType::Effect, "svcA", 6600, 700);
    pulse.effect = "pulse";

    m.scenario.duration = 12000;
    m.scenario.steps    = {
        Message("s1", "svcA", "svcB", "request", "запрос", 300, 1100), // 1. A -> B: request
        State("s2", "svcB", "down", 1400, 10600),                      // 2. B is down
        Note("s2b", "Сервис B недоступен", 470, 20, 1500, 5100),
        timer, // 3. 5 s timer and retries
        Message("s4", "svcA", "svcB", "retry", "retry 1", 2400, 900),
        Message("s5e", "svcB", "svcA", "error", "нет ответа", 4750, 700),
        Message("s5", "svcA", "svcB", "retry", "retry 2", 3800, 900),
        Message("s6", "svcA", "svcB", "retry", "retry 3", 5200, 900),
        pulse, // 4. timeout -> C
        Note("s7n", "5с истекли — переключение на C", 60, 210, 6600, 2200),
        Message("s8", "svcA", "svcC", "request", "запрос", 7000, 1100),
        State("s9", "svcC", "active", 8100, 3900),
        Message("s10", "svcC", "svcA", "success", "200 OK", 8400, 1000),
        State("s11", "svcA", "success", 9500, 2500),
    };
    std::ranges::stable_sort(m.scenario.steps, {}, &Step::start);
    return m;
}

} // namespace ad
