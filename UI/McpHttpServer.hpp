#ifndef _UI_MCP_HTTP_SERVER_HPP_
#define _UI_MCP_HTTP_SERVER_HPP_

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;

/// @file McpHttpServer.hpp
/// @brief MCP "Streamable HTTP" transport on localhost (POST /mcp -> JSON response).
///
/// Security: binds to 127.0.0.1 only, rejects requests whose Origin is not local
/// (DNS rebinding protection) and optionally requires a bearer token.

namespace ad::mcp
{
class McpServer;
}

namespace ad::ui
{

class McpHttpServer : public QObject
{
    Q_OBJECT

public:
    explicit McpHttpServer(mcp::McpServer &server, QObject *parent = nullptr);
    ~McpHttpServer() override;

    bool                  Start(quint16 port, QString *error);
    void                  Stop();
    [[nodiscard]] bool    IsRunning() const;
    [[nodiscard]] quint16 Port() const;
    [[nodiscard]] QString Url() const;
    /// Require "Authorization: Bearer <token>" (empty -- no auth).
    void SetToken(const QString &token) { _token = token; }

    /// Build the HTTP response for a complete request (exposed for tests).
    QByteArray HandleRequest(const QByteArray &method, const QByteArray &path, const QHash<QByteArray, QByteArray> &headers,
                             const QByteArray &body);

Q_SIGNALS:
    void RequestHandled(const QString &summary);

private:
    void _OnNewConnection();
    void _OnReadyRead(QTcpSocket *socket);

    mcp::McpServer                 &_server;
    QTcpServer                     *_tcp = nullptr;
    QHash<QTcpSocket *, QByteArray> _buffers;
    QString                         _token;
};

} // namespace ad::ui

#endif // _UI_MCP_HTTP_SERVER_HPP_
