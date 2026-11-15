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

    /// Build the HTTP response for a complete request.
    QByteArray _HandleRequest(const QByteArray &method, const QByteArray &path, const QHash<QByteArray, QByteArray> &headers,
                              const QByteArray &body);

    void _OnNewConnection();
    void _OnReadyRead(QTcpSocket *socket);
    /// Dispatch complete requests FIFO on the GUI thread, between modal dialogs and menus.
    /// A synchronous export may enter a nested loop; _busy keeps requests queued during it.
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
