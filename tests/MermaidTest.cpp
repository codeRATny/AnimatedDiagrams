#include <gtest/gtest.h>

#include <algorithm>

#include "Export/MermaidExporter.hpp"
#include "Import/MermaidImporter.hpp"
#include "Model/Markers.hpp"
#include "Model/Sample.hpp"

using namespace ad;

namespace
{

constexpr std::string_view kFlowchart = R"(flowchart LR
  client([Client]) -->|REST| gw{API Gateway}
  gw --> svc[Orders<br>Service]
  svc -.->|async| q@{ shape: h-cyl, label: "Queue" }
  svc ==> db[(Postgres)]
  db <--> replica[(Replica)]
  gw --o audit[Audit]
  subgraph backend [Backend]
    svc
    db
  end
  classDef hot fill:#ff9966,stroke:#333
  class db hot
  style gw fill:#bbf,stroke-width:3px
  linkStyle 0 stroke:#ff0000
)";

constexpr std::string_view kSequence = R"(sequenceDiagram
  title Checkout
  actor U as User
  participant W as Web
  participant P as Payments
  participant DB as Database
  U->>W: Pay
  activate W
  W->>P: charge
  Note right of P: retries up to 3 times
  alt card ok
    P-->>W: 200 OK
  else declined
    P--xW: 402
  end
  par audit
    W-)DB: INSERT audit
  and notify
    W-)U: email
  end
  W->>W: render receipt
  deactivate W
  W-->>U: done
)";

const Step *FindStep(const Model &m, std::string_view label)
{
    const auto it = std::ranges::find_if(m.scenario.steps,
                                         [&](const Step &s)
                                         {
                                             return s.label == label || s.text == label;
                                         });
    return it != m.scenario.steps.end() ? &*it : nullptr;
}

} // namespace

TEST(MermaidBlocksTest, MarkdownFencesAndPlainText)
{
    EXPECT_EQ(MermaidBlocks("graph TD\nA-->B\n"), std::vector<std::string>{"graph TD\nA-->B\n"});
    const auto blocks = MermaidBlocks(
        "# Title\n\ntext\n```mermaid\nflowchart LR\n  A --> B\n```\n\n~~~ mermaid\nsequenceDiagram\n  A->>B: hi\n~~~\n```js\nx\n```\n");
    ASSERT_EQ(blocks.size(), 2U);
    EXPECT_EQ(blocks[0], "flowchart LR\n  A --> B\n");
    EXPECT_EQ(blocks[1], "sequenceDiagram\n  A->>B: hi\n");
}

TEST(MermaidExportTest, FlowchartShapesArrowsAndStyles)
{
    Model m;
    m.meta.name = "Shop \"v2\"";
    Node a;
    a.id    = "end"; // a Mermaid keyword
    a.label = "Web \"app\"";
    a.x     = 0;
    a.y     = 0;
    Node b;
    b.id         = "db-1";
    b.type       = "db";
    b.label      = "Orders";
    b.x          = 300;
    b.style.fill = "#112233";
    m.nodes      = {a, b};
    Edge e;
    e.id                 = "e1";
    e.from               = "end";
    e.to                 = "db-1";
    e.label              = "SQL";
    e.style.stroke_style = "dashed";
    e.style.color        = "#ff0000";
    m.edges              = {e};
    const Registry    reg;
    const std::string text = ExportMermaidFlowchart(m, reg);
    EXPECT_NE(text.find("title: \"Shop \\\"v2\\\"\""), std::string::npos) << text;
    EXPECT_NE(text.find("flowchart LR"), std::string::npos);
    EXPECT_NE(text.find("n_end(\"Web #quot;app#quot;\")"), std::string::npos) << text;
    EXPECT_NE(text.find("db_1[(\"Orders\")]"), std::string::npos) << text;
    EXPECT_NE(text.find("n_end -.->|\"SQL\"| db_1"), std::string::npos) << text;
    EXPECT_NE(text.find("style db_1 fill:#112233"), std::string::npos) << text;
    EXPECT_NE(text.find("linkStyle 0 stroke:#ff0000"), std::string::npos) << text;
    EXPECT_NE(text.find("%% ad:pos db_1 300 0"), std::string::npos) << text;
}

TEST(MermaidExportTest, SequenceFromTheSample)
{
    const Model       m    = SampleModel();
    const std::string text = ExportMermaidSequence(m);
    EXPECT_TRUE(text.starts_with("sequenceDiagram\n")) << text;
    EXPECT_NE(text.find("participant svcA as Service A"), std::string::npos) << text;
    EXPECT_NE(text.find("svcA ->> svcB"), std::string::npos) << text;
    EXPECT_NE(text.find("⏱ "), std::string::npos) << text;     // the timeout timer
    EXPECT_NE(text.find("● down"), std::string::npos) << text; // Service B goes down
}

TEST(MermaidImportTest, AvailableInThisBuild)
{
#ifdef AD_TEST_MERMAID_IMPORT
    EXPECT_TRUE(MermaidImportAvailable());
#else
    GTEST_SKIP() << "built without the Mermaid parser";
#endif
}

class MermaidImport : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!MermaidImportAvailable())
        {
            GTEST_SKIP() << "built without the Mermaid parser (WITH_MERMAID=OFF)";
        }
    }
};

TEST_F(MermaidImport, Flowchart)
{
    MermaidImportReport rep;
    const auto          m = ImportMermaid(kFlowchart, {}, &rep);
    ASSERT_TRUE(m.has_value()) << m.error();
    EXPECT_EQ(rep.kinds, "flowchart");
    EXPECT_EQ(rep.nodes, 7);
    EXPECT_EQ(rep.edges, 6);
    EXPECT_EQ(rep.skipped, 1); // the subgraph frame
    EXPECT_EQ(m->FindNode("client")->style.shape, "rounded");
    EXPECT_EQ(m->FindNode("gw")->type, "decision");
    EXPECT_EQ(m->FindNode("gw")->style.fill, "#bbbbff");
    EXPECT_EQ(m->FindNode("gw")->style.stroke_width, 3);
    EXPECT_EQ(m->FindNode("svc")->label, "Orders");
    EXPECT_EQ(m->FindNode("svc")->subtitle, "Service");
    EXPECT_EQ(m->FindNode("q")->type, "queue");
    EXPECT_EQ(m->FindNode("db")->type, "db");
    EXPECT_EQ(m->FindNode("db")->style.fill, "#ff9966");
    const Edge *rest = m->EdgeBetween("client", "gw");
    ASSERT_NE(rest, nullptr);
    EXPECT_EQ(rest->label, "REST");
    EXPECT_EQ(rest->style.color, "#ff0000");
    EXPECT_EQ(m->EdgeBetween("svc", "q")->style.stroke_style, "dashed");
    EXPECT_EQ(m->EdgeBetween("svc", "db")->style.width, 3);
    EXPECT_EQ(m->EdgeBetween("db", "replica")->style.arrow_start, "triangle");
    EXPECT_EQ(m->EdgeBetween("gw", "audit")->style.arrow_end, "circle");
    // left to right, as Mermaid lays it out
    EXPECT_LT(m->FindNode("client")->x, m->FindNode("gw")->x);
    EXPECT_LT(m->FindNode("gw")->x, m->FindNode("svc")->x);
    // no overlaps
    for (const Node &a : m->nodes)
    {
        for (const Node &b : m->nodes)
        {
            if (&a != &b)
            {
                const bool overlap = a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
                EXPECT_FALSE(overlap) << a.id << " / " << b.id;
            }
        }
    }
}

TEST_F(MermaidImport, SequenceBecomesAScenario)
{
    MermaidImportReport rep;
    const auto          m = ImportMermaid(kSequence, {}, &rep);
    ASSERT_TRUE(m.has_value()) << m.error();
    EXPECT_EQ(rep.kinds, "sequence");
    EXPECT_EQ(m->meta.name, "Checkout");
    ASSERT_EQ(m->nodes.size(), 4U);
    EXPECT_EQ(m->FindNode("U")->type, "user");
    EXPECT_EQ(m->FindNode("U")->label, "User");
    EXPECT_LT(m->FindNode("U")->x, m->FindNode("W")->x); // participants' order
    EXPECT_NE(m->EdgeBetween("U", "W"), nullptr);
    EXPECT_NE(m->EdgeBetween("W", "DB"), nullptr);

    const Step *pay = FindStep(*m, "Pay");
    ASSERT_NE(pay, nullptr);
    EXPECT_EQ(pay->type, StepType::Message);
    EXPECT_EQ(pay->variant, "request");
    EXPECT_EQ(FindStep(*m, "200 OK")->variant, "success");
    EXPECT_EQ(FindStep(*m, "402")->variant, "error");
    EXPECT_EQ(FindStep(*m, "done")->variant, "response");
    EXPECT_EQ(FindStep(*m, "render receipt")->type, StepType::Action);
    const Step *note = FindStep(*m, "retries up to 3 times");
    ASSERT_NE(note, nullptr);
    EXPECT_EQ(note->type, StepType::Note);
    // par branches start together
    const Step *audit = FindStep(*m, "INSERT audit");
    const Step *email = FindStep(*m, "email");
    ASSERT_NE(audit, nullptr);
    ASSERT_NE(email, nullptr);
    EXPECT_EQ(audit->variant, "event");
    EXPECT_DOUBLE_EQ(audit->start, email->start);
    EXPECT_GT(FindStep(*m, "render receipt")->start, audit->start);
    // in order
    EXPECT_LT(pay->start, FindStep(*m, "charge")->start);
    EXPECT_LT(FindStep(*m, "charge")->start, FindStep(*m, "200 OK")->start);
    // W active from "activate" to "deactivate"
    const auto active = std::ranges::find_if(m->scenario.steps,
                                             [](const Step &s)
                                             {
                                                 return s.type == StepType::State && s.node_id == "W";
                                             });
    ASSERT_NE(active, m->scenario.steps.end());
    EXPECT_EQ(active->state, "active");
    EXPECT_LE(active->start, FindStep(*m, "charge")->start);
    EXPECT_GE(active->End(), FindStep(*m, "render receipt")->End());
    // alt / else / par sections are chapters
    std::vector<std::string> labels;
    for (const Marker &mk : m->scenario.markers)
    {
        labels.push_back(mk.label);
    }
    EXPECT_EQ(labels, (std::vector<std::string>{"alt: card ok", "else: declined", "par: audit"}));
    EXPECT_GE(m->scenario.duration, FindStep(*m, "done")->End());
}

TEST_F(MermaidImport, SyntaxErrorHasAPosition)
{
    const auto m = ImportMermaid("flowchart LR\n  A --> B[oops\n");
    ASSERT_FALSE(m.has_value());
    EXPECT_NE(m.error().find("line 2"), std::string::npos) << m.error();
}

TEST_F(MermaidImport, UnsupportedAndUnknownDiagrams)
{
    const auto cls = ImportMermaid("classDiagram\n  A <|-- B\n");
    ASSERT_FALSE(cls.has_value());
    EXPECT_NE(cls.error().find("classDiagram"), std::string::npos) << cls.error();
    EXPECT_FALSE(ImportMermaid("hello world").has_value());
    // in Markdown, an unsupported block is skipped
    MermaidImportReport rep;
    const auto          m = ImportMermaid("```mermaid\npie\n  \"a\": 1\n```\n```mermaid\ngraph TD\n  A-->B\n```\n", {}, &rep);
    ASSERT_TRUE(m.has_value()) << m.error();
    EXPECT_EQ(m->nodes.size(), 2U);
    EXPECT_EQ(rep.skipped, 1);
}

TEST_F(MermaidImport, MarkdownExportRoundTrip)
{
    const Model         original = SampleModel();
    const Registry      reg;
    const auto          md = ExportMermaid(original, reg, MermaidKind::Markdown);
    MermaidImportReport rep;
    const auto          back = ImportMermaid(md, {}, &rep);
    ASSERT_TRUE(back.has_value()) << back.error() << "\n" << md;
    EXPECT_EQ(rep.kinds, "flowchart + sequence");
    EXPECT_EQ(back->meta.name, original.meta.name);
    ASSERT_EQ(back->nodes.size(), original.nodes.size()) << md;
    for (const Node &n : original.nodes)
    {
        const Node *b = back->FindNode(n.id);
        ASSERT_NE(b, nullptr) << n.id;
        EXPECT_EQ(b->label, n.label);
        EXPECT_DOUBLE_EQ(b->x, n.x); // "%% ad:pos" comments
        EXPECT_DOUBLE_EQ(b->y, n.y);
    }
    EXPECT_EQ(back->edges.size(), original.edges.size());
    // messages keep their order, endpoints and labels
    std::vector<std::tuple<std::string, std::string, std::string>> want;
    std::vector<std::tuple<std::string, std::string, std::string>> got;
    auto                                                           messages = [](const Model &m, auto &out)
    {
        std::vector<const Step *> s;
        for (const Step &st : m.scenario.steps)
        {
            if (st.type == StepType::Message)
            {
                s.push_back(&st);
            }
        }
        std::ranges::stable_sort(s, {}, &Step::start);
        for (const Step *st : s)
        {
            out.emplace_back(st->from, st->to, st->label);
        }
    };
    messages(original, want);
    messages(*back, got);
    EXPECT_EQ(got, want) << md;
    // timers, states and markers come back too
    const auto count = [](const Model &m, StepType t)
    {
        return std::ranges::count(m.scenario.steps, t, &Step::type);
    };
    EXPECT_EQ(count(*back, StepType::Timer), count(original, StepType::Timer)) << md;
    EXPECT_EQ(count(*back, StepType::State), count(original, StepType::State)) << md;
    EXPECT_EQ(back->scenario.markers.size(), original.scenario.markers.size()) << md;
}

TEST_F(MermaidImport, ExportedFlowchartParsesWithEveryShape)
{
    Model m;
    int   i = 0;
    for (const char *type :
         {"service", "client", "gateway", "external", "user", "db", "queue", "cache", "document", "cloud", "decision", "note"})
    {
        Node n;
        n.id    = std::string(type) + "_node";
        n.type  = type;
        n.label = std::string("Label \"") + type + "\"; #1";
        n.x     = 200.0 * i++;
        m.nodes.push_back(n);
    }
    for (size_t k = 1; k < m.nodes.size(); ++k)
    {
        Edge e;
        e.id                 = "e" + std::to_string(k);
        e.from               = m.nodes[k - 1].id;
        e.to                 = m.nodes[k].id;
        e.label              = k % 2 == 0 ? "a | b" : "";
        e.style.arrow_end    = k % 3 == 0 ? "none" : k % 3 == 1 ? "circle" : "triangle";
        e.style.arrow_start  = k % 4 == 0 ? std::optional<std::string>("triangle") : std::nullopt;
        e.style.stroke_style = k % 5 == 0 ? std::optional<std::string>("dashed") : std::nullopt;
        m.edges.push_back(e);
    }
    const Registry reg;
    const auto     text = ExportMermaidFlowchart(m, reg);
    const auto     back = ImportMermaid(text);
    ASSERT_TRUE(back.has_value()) << back.error() << "\n" << text;
    EXPECT_EQ(back->nodes.size(), m.nodes.size()) << text;
    EXPECT_EQ(back->edges.size(), m.edges.size()) << text;
    EXPECT_EQ(back->FindNode("db_node")->type, "db");
    EXPECT_EQ(back->FindNode("queue_node")->type, "queue");
    EXPECT_EQ(back->FindNode("decision_node")->type, "decision");
    EXPECT_EQ(back->FindNode("document_node")->type, "document");
    EXPECT_EQ(back->FindNode("service_node")->label, "Label \"service\"; #1");
}
