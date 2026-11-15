#include <gtest/gtest.h>

#include <QDialog>
#include <QFile>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtTest/QTest>

#include <algorithm>
#include <memory>
#include <set>

#include "AppContext.hpp"
#include "Controller.hpp"
#include "ExportManager.hpp"
#include "MainWindow.hpp"
#include "Mcp/McpServer.hpp"

using namespace ad;
using namespace ad::ui;
using mcp::Json;

namespace
{

class HttpCall
{
public:
    HttpCall(quint16 port, std::string_view tool, const Json &arguments = Json::object())
    {
        const Json rpc{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"}, {"params", {{"name", tool}, {"arguments", arguments}}}};
        const auto payload = QByteArray::fromStdString(rpc.dump());
        const auto request =
            "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n" + payload;
        QObject::connect(&_socket, &QTcpSocket::connected, &_socket,
                         [this, request]
                         {
                             _socket.write(request);
                         });
        _socket.connectToHost(QHostAddress::LocalHost, port);
    }

    bool Done() const { return _socket.state() == QAbstractSocket::UnconnectedState; }
    bool HasResponse() const { return _socket.bytesAvailable() > 0; }

    Json Result()
    {
        if (!QTest::qWaitFor(
                [this]
                {
                    return Done();
                },
                4000))
        {
            ADD_FAILURE() << "Timed out waiting for the HTTP reply";
            return {};
        }
        const QByteArray response = _socket.readAll();
        const auto       end      = response.indexOf("\r\n\r\n");
        if (!response.startsWith("HTTP/1.1 200") || end < 0)
        {
            ADD_FAILURE() << "Invalid HTTP reply: " << response.toStdString();
            return {};
        }
        return Json::parse(response.mid(end + 4).toStdString()).at("result");
    }

private:
    QTcpSocket _socket;
};

Json Call(quint16 port, std::string_view tool, const Json &arguments = Json::object()) { return HttpCall(port, tool, arguments).Result(); }

class McpWorkspaceTest : public testing::Test
{
protected:
    void SetUp() override
    {
        a = window.NewDocumentTab();
        b = window.NewDocumentTab();
        window.Activate(a);
        QString error;
        if (!context.StartMcp(0, &error))
        {
            QTcpServer probe;
            if (!probe.listen(QHostAddress::LocalHost, 0))
            {
                GTEST_SKIP() << "Loopback sockets unavailable: " << probe.errorString().toStdString();
            }
            FAIL() << error.toStdString();
        }
        port = static_cast<quint16>(QUrl(context.McpUrl()).port());
        ASSERT_NE(port, 0);
    }

    AppContext  context;
    MainWindow  window{context};
    Controller *a    = nullptr;
    Controller *b    = nullptr;
    quint16     port = 0;
};

} // namespace

TEST_F(McpWorkspaceTest, TenAgentsCommitInOrderAcrossTwoTabsAndRestartClearsSessions)
{
    std::vector<std::unique_ptr<HttpCall>> registrations;
    for (int i = 0; i < 10; ++i)
    {
        registrations.push_back(std::make_unique<HttpCall>(port, "register_agent"));
    }
    std::vector<std::string> ids;
    for (auto &registration : registrations)
    {
        const auto result = registration->Result();
        ASSERT_FALSE(result.at("isError").get<bool>());
        ids.push_back(result.at("structuredContent").at("agentId").get<std::string>());
    }
    const std::set distinct(ids.begin(), ids.end());
    ASSERT_EQ(distinct.size(), 10U);

    for (std::size_t i = 5; i < ids.size(); ++i)
    {
        const auto result = Call(port, "select_document", {{"index", 1}, {"agentId", ids[i]}});
        ASSERT_FALSE(result.at("isError").get<bool>());
        EXPECT_EQ(result.at("structuredContent").at("revision"), b->Doc().Revision());
    }
    EXPECT_EQ(context.Active(), a);
    ASSERT_EQ(context.DocumentAgents(a).size(), 5);
    ASSERT_EQ(context.DocumentAgents(b).size(), 5);

    std::vector<std::size_t> pending{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    for (int round = 0; round < 5; ++round)
    {
        const auto                             rev_a = a->Doc().Revision();
        const auto                             rev_b = b->Doc().Revision();
        std::vector<std::unique_ptr<HttpCall>> edits;
        for (auto index : pending)
        {
            edits.push_back(std::make_unique<HttpCall>(
                port, "add_node", Json{{"agentId", ids[index]}, {"label", ids[index]}, {"expectedRevision", index < 5 ? rev_a : rev_b}}));
        }
        std::vector<std::size_t> rejected;
        for (std::size_t i = 0; i < edits.size(); ++i)
        {
            const auto  result     = edits[i]->Result();
            const auto  index      = pending[i];
            auto       *target     = index < 5 ? a : b;
            const auto &structured = result.at("structuredContent");
            ASSERT_EQ(structured.at("revision"), target->Doc().Revision());
            if (result.at("isError").get<bool>())
            {
                EXPECT_EQ(structured.at("code"), "revision_conflict");
                rejected.push_back(index);
            }
            else
            {
                const auto *node = target->GetModel().FindNode(structured.at("id").get<std::string>());
                ASSERT_NE(node, nullptr);
                EXPECT_EQ(node->label, ids[index]);
            }
        }
        EXPECT_EQ(a->GetModel().nodes.size(), static_cast<std::size_t>(round + 1));
        EXPECT_EQ(b->GetModel().nodes.size(), static_cast<std::size_t>(round + 1));
        pending = std::move(rejected);
    }
    EXPECT_TRUE(pending.empty());

    EXPECT_FALSE(Call(port, "unregister_agent", {{"agentId", ids.front()}}).at("isError").get<bool>());
    EXPECT_EQ(context.DocumentAgents(a).size(), 4);
    const auto old_port = port;
    QString    error;
    ASSERT_TRUE(context.StartMcp(0, &error)) << error.toStdString();
    port = static_cast<quint16>(QUrl(context.McpUrl()).port());
    if (port != old_port)
    {
        HttpCall disconnected(old_port, "register_agent");
        ASSERT_TRUE(QTest::qWaitFor(
            [&]
            {
                return disconnected.Done();
            },
            4000));
        EXPECT_FALSE(disconnected.HasResponse());
    }
    EXPECT_TRUE(context.DocumentAgents(a).isEmpty());
    EXPECT_TRUE(context.DocumentAgents(b).isEmpty());
    for (const auto &id : ids)
    {
        EXPECT_TRUE(Call(port, "get_document", {{"agentId", id}}).at("isError").get<bool>());
    }

    ASSERT_TRUE(window.close());
    EXPECT_FALSE(context.IsMcpRunning());
    EXPECT_FALSE(context.StartMcp(0, &error));
    HttpCall stopped(port, "register_agent");
    ASSERT_TRUE(QTest::qWaitFor(
        [&]
        {
            return stopped.Done();
        },
        4000));
    EXPECT_FALSE(stopped.HasResponse());
}

TEST_F(McpWorkspaceTest, ModalLoopDefersHttpAndUsesOriginalAgentTabAfterGuiEdit)
{
    const auto                id         = Call(port, "register_agent").at("structuredContent").at("agentId");
    const auto                revision   = a->Doc().Revision();
    const auto                b_revision = b->Doc().Revision();
    std::unique_ptr<HttpCall> edit;

    QDialog modal(&window);
    QTimer::singleShot(0, &modal,
                       [&]
                       {
                           edit = std::make_unique<HttpCall>(port, "set_scene",
                                                             Json{{"agentId", id}, {"name", "HTTP edit"}, {"expectedRevision", revision}});
                           QTest::qWait(150); // allow socket signals and one full retry while modal
                           EXPECT_FALSE(edit->Done());
                           EXPECT_FALSE(edit->HasResponse());
                           EXPECT_EQ(a->Doc().Revision(), revision);
                           a->Rename(QStringLiteral("GUI edit"));
                           window.Activate(b);
                           modal.accept();
                       });
    modal.exec();

    ASSERT_NE(edit, nullptr);
    const auto result = edit->Result();
    EXPECT_TRUE(result.at("isError").get<bool>());
    EXPECT_EQ(result.at("structuredContent").at("code"), "revision_conflict");
    EXPECT_EQ(result.at("structuredContent").at("revision"), a->Doc().Revision());
    EXPECT_EQ(a->GetModel().meta.name, "GUI edit");
    EXPECT_EQ(b->Doc().Revision(), b_revision);
    EXPECT_EQ(context.Active(), b);
}

TEST_F(McpWorkspaceTest, ExportKeepsItsSnapshotWhileGuiEditsAndHttpWaits)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("snapshot.md"));
    a->Rename(QStringLiteral("Before export"));
    const auto                id       = Call(port, "register_agent").at("structuredContent").at("agentId");
    const auto                revision = a->Doc().Revision();
    std::unique_ptr<HttpCall> queued;

    QObject::connect(&context.Exports(), &ExportManager::JobsChanged, &window,
                     [&]
                     {
                         if (queued)
                         {
                             return;
                         }
                         queued = std::make_unique<HttpCall>(port, "set_scene",
                                                             Json{{"agentId", id}, {"name", "HTTP edit"}, {"expectedRevision", revision}});
                         QTest::qWait(120); // process the second HTTP request inside the first one's call stack
                         EXPECT_FALSE(queued->HasResponse());
                         const auto direct = context.Mcp().CallTool("set_scene", {{"name", "Reentrant edit"}});
                         EXPECT_TRUE(direct.is_error);
                         a->Rename(QStringLiteral("During export"));
                         window.Activate(b);
                     });

    const auto call = Call(port, "export_animation", {{"path", path.toStdString()}, {"format", "mermaid"}, {"agentId", id}});
    ASSERT_FALSE(call.at("isError").get<bool>());
    ASSERT_NE(queued, nullptr);
    const auto result = queued->Result();
    EXPECT_EQ(result.at("structuredContent").at("code"), "revision_conflict");
    EXPECT_EQ(result.at("structuredContent").at("revision"), a->Doc().Revision());
    EXPECT_EQ(a->GetModel().meta.name, "During export");
    EXPECT_EQ(context.Active(), b);
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    EXPECT_TRUE(bytes.contains("Before export"));
    EXPECT_FALSE(bytes.contains("During export"));
}
