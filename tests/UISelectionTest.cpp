#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QStandardPaths>
#include <QtTest/QTest>

#include <nlohmann/json.hpp>

#include "AppContext.hpp"
#include "CanvasWidget.hpp"
#include "Controller.hpp"
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
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {2, 2});
    QTest::mouseMove(&canvas, {170, 70});
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {170, 70});
    ASSERT_EQ(controller.GetSelection().node_ids.size(), 2u);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(&canvas, {50, 50});
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {50, 50});
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
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(&canvas, {50, 50});
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {50, 50});
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
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(&canvas, {50, 50});
    EXPECT_EQ(controller.GetModel(), m);
    canvas.CancelInteraction();
    EXPECT_EQ(controller.GetModel(), m);
    EXPECT_FALSE(controller.Doc().CanUndo());

    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(&canvas, {50, 50});
    controller.Edit({},
                    [](Model &model)
                    {
                        model.meta.name = "External";
                    });
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {50, 50});
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
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(&canvas, {50, 50});
    Model replacement     = model;
    replacement.meta.name = "Another document";
    controller.ReplaceModel(replacement, {}, false);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {50, 50});
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
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::ControlModifier, {120, 30});
    EXPECT_EQ(controller.GetSelection().id, "a");
    ASSERT_EQ(controller.GetSelection().node_ids.size(), 2u);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::ShiftModifier, {300, 100});
    QTest::mouseMove(&canvas, {350, 150});
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::ShiftModifier, {350, 150});
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
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(&canvas, {50, 50});
    controller.Undo();
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {50, 50});
    EXPECT_EQ(controller.GetModel(), model);
    EXPECT_TRUE(controller.Doc().CanRedo());
    controller.Redo();
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(&canvas, {50, 50});
    canvas.ZoomBy(1.1);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {50, 50});
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
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {30, 30});
    QTest::mouseMove(&canvas, {50, 50});
    EXPECT_FALSE(context.Mcp().CallTool("undo", nlohmann::json::object()).is_error);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {50, 50});
    EXPECT_EQ(controller.GetModel(), model);
    EXPECT_TRUE(controller.Doc().CanRedo());
}

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
