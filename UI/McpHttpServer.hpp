#ifndef _UI_MCP_HTTP_SERVER_HPP_
#define _UI_MCP_HTTP_SERVER_HPP_

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QString>
#include <QTcpSocket>

class QTcpServer;
class QTimer;

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
    struct Request
    {
        QPointer<QTcpSocket>          socket; // may go away while the request waits or runs
        QByteArray                    method;
        QByteArray                    path;
        QHash<QByteArray, QByteArray> headers;
        QByteArray                    body;
    };

    void _OnNewConnection();
    void _OnReadyRead(QTcpSocket *socket);
    /// Handle queued requests one by one. Requests never run inside another event loop:
    /// a tool may run one (export waits for its background job) and so do modal dialogs
    /// and menus, whose code holds pointers into the model. Requests arriving meanwhile
    /// wait in the queue and run when the loop is over.
    void _ProcessQueue();

    mcp::McpServer                 &_server;
    QTcpServer                     *_tcp = nullptr;
    QHash<QTcpSocket *, QByteArray> _buffers;
    QQueue<Request>                 _queue;
    bool                            _busy  = false;
    QTimer                         *_retry = nullptr; // re-check after a modal dialog / menu
    QString                         _token;
};

} // namespace ad::ui

#endif // _UI_MCP_HTTP_SERVER_HPP_
