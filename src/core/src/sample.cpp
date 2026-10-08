#include "ad/sample.hpp"

#include <algorithm>

namespace ad {

namespace {

Node service(std::string id, std::string label, double x, double y) {
    Node n;
    n.id = std::move(id);
    n.label = std::move(label);
    n.kind = "service";
    n.x = x;
    n.y = y;
    n.w = 150;
    n.h = 66;
    n.color = nodeKind("service").color.hex();
    n.shape = "round";
    return n;
}

Step step(std::string id, StepType type, double start, double duration) {
    Step s;
    s.id = std::move(id);
    s.type = type;
    s.start = start;
    s.duration = duration;
    return s;
}

Step message(std::string id, std::string from, std::string to, std::string variant, std::string label, double start,
             double duration) {
    Step s = step(std::move(id), StepType::Message, start, duration);
    s.from = std::move(from);
    s.to = std::move(to);
    s.variant = std::move(variant);
    s.label = std::move(label);
    return s;
}

Step onNode(std::string id, StepType type, std::string node, double start, double duration) {
    Step s = step(std::move(id), type, start, duration);
    s.nodeId = std::move(node);
    return s;
}

Step state(std::string id, std::string node, std::string st, double start, double duration) {
    Step s = onNode(std::move(id), StepType::State, std::move(node), start, duration);
    s.state = std::move(st);
    return s;
}

Step note(std::string id, std::string text, double x, double y, double start, double duration) {
    Step s = step(std::move(id), StepType::Note, start, duration);
    s.text = std::move(text);
    s.x = x;
    s.y = y;
    return s;
}

}  // namespace

Model emptyModel() { return Model{}; }

Model sampleModel() {
    Model m;
    m.meta.name = "Пример: fallback с ретраями и таймаутом";
    m.nodes = {service("svcA", "Сервис A", 80, 120), service("svcB", "Сервис B", 480, 60),
               service("svcC", "Сервис C", 480, 260)};

    Edge ab;
    ab.id = "eAB";
    ab.from = "svcA";
    ab.to = "svcB";
    ab.label = "REST";
    ab.curve = 0.15;
    Edge ac;
    ac.id = "eAC";
    ac.from = "svcA";
    ac.to = "svcC";
    ac.label = "fallback";
    ac.style = "dashed";
    ac.curve = -0.15;
    m.edges = {ab, ac};

    Step timer = onNode("s3", StepType::Timer, "svcA", 1600, 5000);
    timer.seconds = 5;
    timer.label = "timeout";

    m.scenario.duration = 12000;
    m.scenario.steps = {
        message("s1", "svcA", "svcB", "request", "запрос", 300, 1100),         // 1. A → B: запрос
        state("s2", "svcB", "down", 1400, 10600),                                // 2. B недоступен
        note("s2b", "Сервис B недоступен", 470, 20, 1500, 5100),
        timer,                                                                   // 3. таймер 5с
        message("s4", "svcA", "svcB", "retry", "retry 1", 2400, 900),           //    и ретраи
        message("s5e", "svcB", "svcA", "error", "нет ответа", 4750, 700),
        message("s5", "svcA", "svcB", "retry", "retry 2", 3800, 900),
        message("s6", "svcA", "svcB", "retry", "retry 3", 5200, 900),
        onNode("s7", StepType::Pulse, "svcA", 6600, 700),                       // 4. таймаут → C
        note("s7n", "5с истекли — переключение на C", 60, 210, 6600, 2200),
        message("s8", "svcA", "svcC", "request", "запрос", 7000, 1100),
        state("s9", "svcC", "active", 8100, 3900),
        message("s10", "svcC", "svcA", "success", "200 OK", 8400, 1000),
        state("s11", "svcA", "success", 9500, 2500),
    };
    std::ranges::stable_sort(m.scenario.steps, {}, &Step::start);
    return m;
}

}  // namespace ad
