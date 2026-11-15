#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QtTest/QTest>

#include <nlohmann/json.hpp>

#include "AppContext.hpp"
#include "CanvasWidget.hpp"
#include "Controller.hpp"
#include "MainWindow.hpp"
#include "Mcp/McpServer.hpp"
#include "Model/Sample.hpp"

using namespace ad;
using namespace ad::ui;

TEST(UISelectionTest, CopyPasteGroupPreservesInternalEdgesAndUndo)
{
    AppContext context;
    Controller source(context);
    Model      m = SampleModel();
    m.nodes[0].ports.push_back({"p_test", 10, 20});
    m.edges[0].from_port = "p_test";
    m.edges[0].waypoints.push_back({100, 100});
    source.ReplaceModel(m, {}, false);
    source.SelectNodes({"svcA", "svcB"}, "svcA");
    source.CopyNodes();
    source.PasteNodes();
    ASSERT_EQ(source.GetSelection().node_ids.size(), 2u);
    const Model pasted = source.GetModel();
    ASSERT_EQ(pasted.nodes.size(), m.nodes.size() + 2);
    ASSERT_EQ(pasted.edges.size(), m.edges.size() + 1);
    const Edge &edge = pasted.edges.back();
    EXPECT_NE(edge.from, "svcA");
    EXPECT_NE(edge.to, "svcB");
    ASSERT_NE(pasted.FindPort(edge.from, edge.from_port), nullptr);
    EXPECT_EQ(edge.waypoints.front(), (Vec2{126, 126}));
    EXPECT_EQ(pasted.scenario, m.scenario);
    source.Undo();
    EXPECT_EQ(source.GetModel(), m);
    source.Redo();
    EXPECT_EQ(source.GetModel(), pasted);
}

TEST(UISelectionTest, PasteWithConflictingDocumentTypeCreatesIndependentDefinition)
{
    AppContext  context;
    Controller  source(context);
    Model       m      = SampleModel();
    ElementType custom = *context.Reg().FindElement("service");
    custom.style.fill  = "#112233";
    m.library.elements.push_back(custom);
    source.ReplaceModel(m, {}, false);
    source.Select(Selection::Kind::Node, "svcA");
    source.CopyNodes();

    Controller destination(context);
    Model      target = SampleModel();
    destination.ReplaceModel(target, {}, false);
    destination.PasteNodes();
    ASSERT_EQ(destination.GetModel().nodes.size(), target.nodes.size() + 1);
    const Node &copy = destination.GetModel().nodes.back();
    EXPECT_NE(copy.type, "service");
    const auto *definition = context.Reg().FindElement(copy.type, &destination.GetModel().library);
    ASSERT_NE(definition, nullptr);
    EXPECT_EQ(definition->style.fill, custom.style.fill);
    destination.Undo();
    EXPECT_EQ(destination.GetModel(), target);
}

TEST(UISelectionTest, RepeatedPasteOffsetsAndSelectionValidation)
{
    AppContext  context;
    Controller  controller(context);
    const Model model = SampleModel();
    controller.ReplaceModel(model, {}, false);
    controller.SelectNodes({"svcA", "svcB"}, "svcA");
    controller.CopyNodes();
    controller.PasteNodes();
    const auto   first = controller.GetSelection().node_ids;
    const double x     = controller.GetModel().FindNode(first.front())->x;
    controller.PasteNodes();
    const auto second = controller.GetSelection().node_ids;
    EXPECT_DOUBLE_EQ(controller.GetModel().FindNode(second.front())->x, x + 26 / model.view.zoom);
    controller.DeleteSelection();
    EXPECT_TRUE(controller.GetSelection().Empty());
    controller.Undo();
    EXPECT_NE(controller.GetModel().FindNode(second.front()), nullptr);
}

TEST(UISelectionTest, ClipboardRejectsInvalidPayload)
{
    AppContext context;
    Controller controller(context);
    controller.ReplaceModel(SampleModel(), {}, false);
    const Model original = controller.GetModel();
    auto       *mime     = new QMimeData;
    mime->setData("application/x-animated-diagrams-nodes", "not json");
    QApplication::clipboard()->setMimeData(mime);
    controller.PasteNodes();
    EXPECT_EQ(controller.GetModel(), original);
    EXPECT_FALSE(controller.Doc().CanUndo());
}

TEST(UISelectionTest, MarqueeAndGroupMove)
{
    AppContext context;
    Controller controller(context);
    Model      m;
    Node       a;
    a.id    = "a";
    a.x     = 10;
    a.y     = 10;
    a.w     = 50;
    a.h     = 40;
    Node b  = a;
    b.id    = "b";
    b.x     = 100;
    m.nodes = {a, b};
    controller.ReplaceModel(m, {}, false);
    CanvasWidget canvas(controller);
    canvas.resize(400, 250);
    canvas.show();
    // The QWindow overload sends mouse moves even on the offscreen Qt 6.2 backend.
    QTest::mousePress(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {2, 2});
    QTest::mouseMove(canvas.windowHandle(), {170, 70});
    QTest::mouseRelease(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {170, 70});
    ASSERT_EQ(controller.GetSelection().node_ids.size(), 2u);
    QTest::mousePress(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(canvas.windowHandle(), {50, 50});
    QTest::mouseRelease(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {50, 50});
    EXPECT_EQ(controller.GetModel().FindNode("a")->x, 30);
    EXPECT_EQ(controller.GetModel().FindNode("b")->x, 120);
    controller.Undo();
    EXPECT_EQ(controller.GetModel(), m);
}

TEST(UISelectionTest, GroupMoveTranslatesOnlyInternalWaypoints)
{
    AppContext context;
    Controller controller(context);
    Model      model;
    Node       a;
    a.id        = "a";
    a.x         = 10;
    a.y         = 10;
    a.w         = 50;
    a.h         = 40;
    Node b      = a;
    b.id        = "b";
    b.x         = 100;
    Node c      = a;
    c.id        = "c";
    c.x         = 200;
    model.nodes = {a, b, c};
    Edge internal;
    internal.id   = "internal";
    internal.from = "a";
    internal.to   = "b";
    internal.waypoints.push_back({80, 80});
    Edge external;
    external.id   = "external";
    external.from = "b";
    external.to   = "c";
    external.waypoints.push_back({170, 80});
    model.edges = {internal, external};
    controller.ReplaceModel(model, {}, false);
    controller.SelectNodes({"a", "b"}, "a");
    CanvasWidget canvas(controller);
    canvas.resize(400, 250);
    canvas.show();
    QTest::mousePress(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(canvas.windowHandle(), {50, 50});
    QTest::mouseRelease(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {50, 50});
    EXPECT_EQ(controller.GetModel().FindEdge("internal")->waypoints[0], (Vec2{100, 100}));
    EXPECT_EQ(controller.GetModel().FindEdge("external")->waypoints[0], (Vec2{170, 80}));
    controller.Undo();
    EXPECT_EQ(controller.GetModel(), model);
}

TEST(UISelectionTest, CancelDragAndPreserveExternalEdits)
{
    AppContext context;
    Controller controller(context);
    Model      m;
    Node       a;
    a.id = "a";
    a.x  = 10;
    a.y  = 10;
    a.w  = 50;
    a.h  = 40;
    m.nodes.push_back(a);
    controller.ReplaceModel(m, {}, false);
    CanvasWidget canvas(controller);
    canvas.resize(400, 250);
    canvas.show();
    QTest::mousePress(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(canvas.windowHandle(), {50, 50});
    EXPECT_EQ(controller.GetModel(), m);
    canvas.CancelInteraction();
    EXPECT_EQ(controller.GetModel(), m);
    EXPECT_FALSE(controller.Doc().CanUndo());

    QTest::mousePress(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(canvas.windowHandle(), {50, 50});
    controller.Edit({},
                    [](Model &model)
                    {
                        model.meta.name = "External";
                    });
    QTest::mouseRelease(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {50, 50});
    EXPECT_EQ(controller.GetModel().meta.name, "External");
    EXPECT_EQ(controller.GetModel().FindNode("a")->x, 30);
    controller.Undo();
    EXPECT_EQ(controller.GetModel().meta.name, "External");
    EXPECT_EQ(controller.GetModel().FindNode("a")->x, 10);
}

TEST(UISelectionTest, ReplacingDocumentCancelsDrag)
{
    AppContext context;
    Controller controller(context);
    Model      model;
    Node       node;
    node.id = "a";
    node.x  = 10;
    node.y  = 10;
    node.w  = 50;
    node.h  = 40;
    model.nodes.push_back(node);
    controller.ReplaceModel(model, {}, false);
    CanvasWidget canvas(controller);
    canvas.resize(400, 250);
    canvas.show();
    QTest::mousePress(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(canvas.windowHandle(), {50, 50});
    Model replacement     = model;
    replacement.meta.name = "Another document";
    controller.ReplaceModel(replacement, {}, false);
    QTest::mouseRelease(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {50, 50});
    EXPECT_EQ(controller.GetModel(), replacement);
    EXPECT_FALSE(controller.Doc().CanUndo());
}

TEST(UISelectionTest, ClipboardPreservesAccentTokensAndRejectsWrongFieldTypes)
{
    AppContext context;
    Controller controller(context);
    Model      model      = SampleModel();
    model.nodes[0].accent = "$primary";
    controller.ReplaceModel(model, {}, false);
    controller.Select(Selection::Kind::Node, model.nodes[0].id);
    controller.CopyNodes();
    const QByteArray bytes = QApplication::clipboard()->mimeData()->data("application/x-animated-diagrams-nodes");
    controller.PasteNodes();
    ASSERT_EQ(controller.GetModel().nodes.size(), model.nodes.size() + 1);
    EXPECT_EQ(controller.GetModel().nodes.back().accent, "$primary");
    controller.Undo();

    auto payload              = nlohmann::json::parse(bytes.toStdString());
    auto fragment             = nlohmann::json::parse(payload["document"].get<std::string>());
    fragment["nodes"][0]["x"] = "broken";
    payload["document"]       = fragment.dump();
    auto *mime                = new QMimeData;
    mime->setData("application/x-animated-diagrams-nodes", QByteArray::fromStdString(payload.dump()));
    QApplication::clipboard()->setMimeData(mime);
    controller.PasteNodes();
    EXPECT_EQ(controller.GetModel(), model);
    EXPECT_FALSE(controller.Doc().CanUndo());
}

TEST(UISelectionTest, ToggleAndEmptyMarqueePreservePrimary)
{
    AppContext context;
    Controller controller(context);
    Model      model;
    Node       node;
    node.id = "a";
    node.x  = 10;
    node.y  = 10;
    node.w  = 50;
    node.h  = 40;
    model.nodes.push_back(node);
    node.id = "b";
    node.x  = 100;
    model.nodes.push_back(node);
    node.id = "c";
    node.x  = 200;
    model.nodes.push_back(node);
    controller.ReplaceModel(model, {}, false);
    controller.SelectNodes({"a", "b", "c"}, "a");
    CanvasWidget canvas(controller);
    canvas.resize(400, 250);
    canvas.show();
    QTest::mouseClick(canvas.windowHandle(), Qt::LeftButton, Qt::ControlModifier, {120, 30});
    EXPECT_EQ(controller.GetSelection().id, "a");
    ASSERT_EQ(controller.GetSelection().node_ids.size(), 2u);
    QTest::mousePress(canvas.windowHandle(), Qt::LeftButton, Qt::ShiftModifier, {300, 100});
    QTest::mouseMove(canvas.windowHandle(), {350, 150});
    QTest::mouseRelease(canvas.windowHandle(), Qt::LeftButton, Qt::ShiftModifier, {350, 150});
    EXPECT_EQ(controller.GetSelection().id, "a");
}

TEST(UISelectionTest, UndoAndZoomCancelDrag)
{
    AppContext context;
    Controller controller(context);
    Model      model;
    Node       node;
    node.id = "a";
    node.x  = 10;
    node.y  = 10;
    node.w  = 50;
    node.h  = 40;
    model.nodes.push_back(node);
    controller.ReplaceModel(model, {}, false);
    controller.Edit({},
                    [](Model &m)
                    {
                        m.meta.name = "Changed";
                    });
    CanvasWidget canvas(controller);
    canvas.resize(400, 250);
    canvas.show();
    QTest::mousePress(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(canvas.windowHandle(), {50, 50});
    controller.Undo();
    QTest::mouseRelease(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {50, 50});
    EXPECT_EQ(controller.GetModel(), model);
    EXPECT_TRUE(controller.Doc().CanRedo());
    controller.Redo();
    QTest::mousePress(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(canvas.windowHandle(), {50, 50});
    canvas.ZoomBy(1.1);
    QTest::mouseRelease(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {50, 50});
    EXPECT_EQ(controller.GetModel().FindNode("a")->x, 10);
    EXPECT_EQ(controller.GetModel().meta.name, "Changed");
}

TEST(UISelectionTest, McpUndoCancelsDragAndPreservesRedo)
{
    AppContext context;
    Controller controller(context);
    context.SetActive(&controller);
    Model model;
    Node  node;
    node.id = "a";
    node.x  = 10;
    node.y  = 10;
    node.w  = 50;
    node.h  = 40;
    model.nodes.push_back(node);
    controller.ReplaceModel(model, {}, false);
    controller.Edit({},
                    [](Model &m)
                    {
                        m.meta.name = "Changed";
                    });
    CanvasWidget canvas(controller);
    canvas.resize(400, 250);
    canvas.show();
    QTest::mousePress(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(canvas.windowHandle(), {50, 50});
    EXPECT_FALSE(context.Mcp().CallTool("undo", nlohmann::json::object()).is_error);
    QTest::mouseRelease(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, {50, 50});
    EXPECT_EQ(controller.GetModel(), model);
    EXPECT_TRUE(controller.Doc().CanRedo());
}

TEST(UISelectionTest, AgentBindingSurvivesTabSwitchAndCancelledClose)
{
    AppContext context;
    MainWindow window(context);
    auto      &mcp   = context.Mcp();
    auto      *first = window.NewDocumentTab();
    ASSERT_NE(first, nullptr);
    const auto agent  = mcp.CallTool("register_agent", {}).structured.at("agentId");
    auto      *second = window.NewDocumentTab();
    ASSERT_FALSE(mcp.CallTool("add_node", {{"agentId", agent}, {"label", "First"}}).is_error);
    ASSERT_EQ(first->GetModel().nodes.back().label, "First");
    EXPECT_EQ(context.Active(), second);

    auto *tabs = window.findChild<QTabWidget *>();
    ASSERT_NE(tabs, nullptr);
    const auto docs         = window.Documents();
    const int  first_index  = std::ranges::find(docs, first) - docs.begin();
    const int  second_index = std::ranges::find(docs, second) - docs.begin();
    EXPECT_TRUE(mcp.CallTool("select_document", {{"agentId", agent}, {"index", -1}}).is_error);
    EXPECT_EQ(context.DocumentAgents(first).size(), 1);
    ASSERT_FALSE(mcp.CallTool("select_document", {{"agentId", agent}, {"index", second_index}}).is_error);
    EXPECT_TRUE(context.DocumentAgents(first).isEmpty());
    window.Activate(first);
    ASSERT_FALSE(mcp.CallTool("add_node", {{"agentId", agent}, {"label", "Second"}}).is_error);
    ASSERT_EQ(second->GetModel().nodes.back().label, "Second");
    EXPECT_EQ(context.Active(), first);

    auto respond = [&](bool confirm_agents, QMessageBox::StandardButton save_response)
    {
        QTimer::singleShot(0, &window,
                           [&, confirm_agents, save_response]
                           {
                               auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                               EXPECT_NE(dialog, nullptr);
                               if (dialog == nullptr)
                               {
                                   return;
                               }
                               EXPECT_TRUE(dialog->text().contains("1"));
                               EXPECT_EQ(dialog->informativeText().toStdString(), agent.get<std::string>());
                               if (!confirm_agents)
                               {
                                   dialog->button(QMessageBox::Cancel)->click();
                                   return;
                               }
                               QTimer::singleShot(0, &window,
                                                  [save_response]
                                                  {
                                                      auto *save = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                                                      EXPECT_NE(save, nullptr);
                                                      if (save != nullptr)
                                                      {
                                                          save->button(save_response)->click();
                                                      }
                                                  });
                               for (auto *button : dialog->buttons())
                               {
                                   if (dialog->buttonRole(button) == QMessageBox::DestructiveRole)
                                   {
                                       button->click();
                                       return;
                                   }
                               }
                               ADD_FAILURE() << "Missing close action";
                               dialog->reject();
                           });
        Q_EMIT tabs->tabCloseRequested(second_index);
    };

    respond(false, QMessageBox::Cancel);
    EXPECT_FALSE(mcp.CallTool("get_document", {{"agentId", agent}}).is_error);
    respond(true, QMessageBox::Cancel);
    EXPECT_FALSE(mcp.CallTool("get_document", {{"agentId", agent}}).is_error);
    EXPECT_EQ(window.Documents().size(), docs.size());
    respond(true, QMessageBox::Discard);
    EXPECT_EQ(window.Documents().size(), docs.size() - 1);
    EXPECT_TRUE(mcp.CallTool("get_document", {{"agentId", agent}}).is_error);
    EXPECT_TRUE(context.DocumentAgents(first).isEmpty());
    EXPECT_NE(first_index, second_index);
}

TEST(UISelectionTest, McpReportsAgentPositionAndTransfers)
{
    using Activity = mcp::DocumentHost::AgentActivity;
    std::vector<Activity> calls;
    AppContext            context;
    MainWindow            window(context);
    auto                 *first = window.NewDocumentTab();
    auto                 &mcp   = context.Mcp();
    QObject::connect(&context, &AppContext::AgentActivity, &window,
                     [&](const Activity &activity)
                     {
                         calls.push_back(activity);
                     });

    const auto id   = mcp.CallTool("register_agent", {}).structured.at("agentId");
    const auto node = mcp.CallTool("add_node", {{"agentId", id}, {"label", "Agent node"}, {"x", 100}, {"y", 80}});
    ASSERT_FALSE(node.is_error);
    const auto node_id = node.structured.at("id").get<std::string>();
    ASSERT_EQ(calls.size(), 1);
    EXPECT_EQ(calls.back().document, first);
    EXPECT_EQ(calls.back().position, first->GetModel().FindNode(node_id)->Bounds().Center());

    calls.clear();
    EXPECT_TRUE(mcp.CallTool("update_node", {{"agentId", id}, {"id", "unknown"}}).is_error);
    ASSERT_EQ(calls.size(), 1);
    EXPECT_FALSE(calls.back().position.has_value());

    calls.clear();
    const auto removed_at = first->GetModel().FindNode(node_id)->Bounds().Center();
    ASSERT_FALSE(mcp.CallTool("remove_node", {{"agentId", id}, {"id", node_id}}).is_error);
    ASSERT_EQ(calls.size(), 1);
    EXPECT_EQ(calls.back().position, removed_at);

    calls.clear();
    ASSERT_FALSE(mcp.CallTool("add_step", {{"agentId", id}, {"type", "note"}, {"text", "Note"}, {"x", 145}, {"y", 320}}).is_error);
    ASSERT_EQ(calls.size(), 1);
    EXPECT_EQ(calls.back().position, (Vec2{145, 320}));

    calls.clear();
    auto      *second    = window.NewDocumentTab();
    const auto documents = window.Documents();
    const auto index     = std::ranges::find(documents, second) - documents.begin();
    ASSERT_FALSE(mcp.CallTool("select_document", {{"agentId", id}, {"index", index}}).is_error);
    ASSERT_EQ(calls.size(), 1);
    EXPECT_EQ(calls.back().previous_document, first);
    EXPECT_EQ(calls.back().document, second);
    ASSERT_FALSE(mcp.CallTool("unregister_agent", {{"agentId", id}}).is_error);
    ASSERT_EQ(calls.size(), 2);
    EXPECT_EQ(calls.back().previous_document, second);
    EXPECT_EQ(calls.back().document, nullptr);
}

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
