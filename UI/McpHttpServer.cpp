#include "McpHttpServer.hpp"

#include <QApplication>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include "Mcp/McpServer.hpp"

namespace ad::ui
{

namespace
{

constexpr qsizetype kMaxRequest = 16 * 1024 * 1024;

QByteArray Response(int code, const QByteArray &reason, const QByteArray &body = {}, const QByteArray &content_type = "application/json")
{
    QByteArray r = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    if (!body.isEmpty())
    {
        r += "Content-Type: " + content_type + "\r\n";
    }
    r += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    r += "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
    r += body;
    return r;
}

/// Origin header is only sent by browsers; accept it when it points to this machine.
bool IsLocalOrigin(const QByteArray &origin)
{
    if (origin.isEmpty() || origin == "null")
    {
        return origin.isEmpty();
    }
    const QString host = QUrl(QString::fromUtf8(origin)).host();
    return host == QStringLiteral("localhost") || host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1");
}

} // namespace

McpHttpServer::McpHttpServer(mcp::McpServer &server, QObject *parent) : QObject(parent), _server(server)
{
    _retry = new QTimer(this);
    _retry->setSingleShot(true);
    _retry->setInterval(100);
    connect(_retry, &QTimer::timeout, this, &McpHttpServer::_ProcessQueue);
}

McpHttpServer::~McpHttpServer() { Stop(); }

bool McpHttpServer::Start(quint16 port, QString *error)
{
    Stop();
    _tcp = new QTcpServer(this);
    connect(_tcp, &QTcpServer::newConnection, this, &McpHttpServer::_OnNewConnection);
    if (!_tcp->listen(QHostAddress::LocalHost, port))
    {
        if (error != nullptr)
        {
            *error = _tcp->errorString();
        }
        delete _tcp;
        _tcp = nullptr;
        return false;
    }
    return true;
}

void McpHttpServer::Stop()
{
    if (_tcp != nullptr)
    {
        _tcp->close();
        delete _tcp;
        _tcp = nullptr;
    }
    // abort() emits disconnected() synchronously and its handler edits _buffers: iterate a copy
    const QList<QTcpSocket *> sockets = _buffers.keys();
    _buffers.clear();
    _queue.clear();
    for (QTcpSocket *socket : sockets)
    {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
}

bool    McpHttpServer::IsRunning() const { return _tcp != nullptr && _tcp->isListening(); }
quint16 McpHttpServer::Port() const { return IsRunning() ? _tcp->serverPort() : 0; }
QString McpHttpServer::Url() const { return IsRunning() ? QStringLiteral("http://127.0.0.1:%1/mcp").arg(Port()) : QString(); }

void McpHttpServer::_OnNewConnection()
{
    while (_tcp != nullptr && _tcp->hasPendingConnections())
    {
        QTcpSocket *socket = _tcp->nextPendingConnection();
        _buffers.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket]
                {
                    _OnReadyRead(socket);
                });
        connect(socket, &QTcpSocket::disconnected, this,
                [this, socket]
                {
                    _buffers.remove(socket);
                    socket->deleteLater();
                });
    }
}

void McpHttpServer::_OnReadyRead(QTcpSocket *socket)
{
    if (!_buffers.contains(socket))
    {
        return;
    }
    QByteArray &buf = _buffers[socket];
    buf += socket->readAll();
    if (buf.size() > kMaxRequest)
    {
        socket->write(Response(413, "Payload Too Large"));
        socket->disconnectFromHost();
        return;
    }
    const qsizetype header_end = buf.indexOf("\r\n\r\n");
    if (header_end < 0)
    {
        return; // wait for the full header
    }
    const QList<QByteArray> lines = buf.left(header_end).split('\n');
    const QList<QByteArray> start = lines.value(0).trimmed().split(' ');
    if (start.size() < 3)
    {
        socket->write(Response(400, "Bad Request"));
        socket->disconnectFromHost();
        return;
    }
    QHash<QByteArray, QByteArray> headers;
    for (qsizetype i = 1; i < lines.size(); ++i)
    {
        const qsizetype colon = lines[i].indexOf(':');
        if (colon > 0)
        {
            headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
        }
    }
    const qsizetype length = headers.value("content-length", "0").toLongLong();
    if (length < 0 || length > kMaxRequest)
    {
        socket->write(Response(413, "Payload Too Large"));
        socket->disconnectFromHost();
        return;
    }
    if (buf.size() < header_end + 4 + length)
    {
        return; // wait for the body
    }
    Request request{socket, start[0], start[1], std::move(headers), buf.mid(header_end + 4, length)};
    buf.clear();
    _queue.enqueue(std::move(request));
    // not from here: this runs inside the socket's readyRead emission and a request may
    // spin an event loop that deletes the socket (client timeout) under that emission
    QMetaObject::invokeMethod(this, &McpHttpServer::_ProcessQueue, Qt::QueuedConnection);
}

void McpHttpServer::_ProcessQueue()
{
    if (_busy)
    {
        return; // the running request picks the rest up when it is done
    }
    _busy = true;
    while (!_queue.isEmpty())
    {
        if (QApplication::activeModalWidget() != nullptr || QApplication::activePopupWidget() != nullptr)
        {
            _retry->start(); // the user is in a dialog or a menu: wait until it closes
            break;
        }
        const Request r = _queue.dequeue();
        if (r.socket == nullptr)
        {
            continue; // the client went away while waiting
        }
        const QByteArray response = HandleRequest(r.method, r.path, r.headers, r.body);
        if (r.socket != nullptr && r.socket->state() == QAbstractSocket::ConnectedState)
        {
            r.socket->write(response);
            r.socket->disconnectFromHost();
        }
    }
    _busy = false;
}

QByteArray McpHttpServer::HandleRequest(const QByteArray &method, const QByteArray &path, const QHash<QByteArray, QByteArray> &headers,
                                        const QByteArray &body)
{
    const QByteArray route = path.split('?').value(0);
    if (method == "GET" && route == "/health")
    {
        return Response(200, "OK", "ok\n", "text/plain");
    }
    if (route != "/mcp" && route != "/")
    {
        return Response(404, "Not Found");
    }
    if (!IsLocalOrigin(headers.value("origin")))
    {
        return Response(403, "Forbidden", R"({"error":"origin not allowed"})");
    }
    if (!_token.isEmpty() && headers.value("authorization") != "Bearer " + _token.toUtf8())
    {
        return Response(401, "Unauthorized", R"({"error":"missing or invalid bearer token"})");
    }
    if (method != "POST")
    {
        // no server-initiated SSE stream: GET / DELETE are not supported (allowed by the spec)
        return Response(405, "Method Not Allowed");
    }
    const std::string response = _server.Handle(std::string_view(body.constData(), static_cast<size_t>(body.size())));
    const auto        parsed   = mcp::Json::parse(body.constData(), body.constData() + body.size(), nullptr, false);
    if (!parsed.is_discarded() && parsed.is_object() && parsed.value("method", "") == "tools/call")
    {
        Q_EMIT RequestHandled(QStringLiteral("MCP: %1").arg(QString::fromStdString(parsed["params"].value("name", ""))));
    }
    if (response.empty())
    {
        return Response(202, "Accepted");
    }
    return Response(200, "OK", QByteArray::fromStdString(response));
}

} // namespace ad::ui
