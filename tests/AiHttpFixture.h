#pragma once
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QHostAddress>
#include <QUrl>
#include <functional>
#include <memory>

// Loopback-only fake provider. No real credentials or external API requests.
class AiHttpFixture : public QObject {
public:
    struct Request { QByteArray path, authorization; QJsonObject json; QPointer<QTcpSocket> socket; };
    QList<Request> requests;
    std::function<void(const Request &)> handler;
    AiHttpFixture() {
        connect(&server,&QTcpServer::newConnection,this,[this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                auto buffer = std::make_shared<QByteArray>();
                auto handled = std::make_shared<bool>(false);
                connect(socket,&QIODevice::readyRead,this,[this,socket,buffer,handled] {
                    *buffer += socket->readAll();
                    if (*handled) return;
                    const int end = buffer->indexOf("\r\n\r\n"); if (end < 0) return;
                    const auto lines = buffer->left(end).split('\n'); int length = 0;
                    Request request; request.socket = socket;
                    request.path = lines.first().split(' ').value(1);
                    for (const auto &line : lines) {
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                        if (line.toLower().startsWith("authorization:")) request.authorization = line.mid(14).trimmed();
                    }
                    if (buffer->size() < end+4+length) return;
                    request.json = QJsonDocument::fromJson(buffer->mid(end+4,length)).object();
                    *handled = true; requests.append(request);
                    if (handler) handler(request);
                });
            }
        });
        server.listen(QHostAddress::LocalHost);
    }
    QUrl base() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/api/v1/").arg(server.serverPort())); }
    static void reply(const Request &request, int status, const QByteArray &body, const QByteArray &type = "application/json") {
        if (!request.socket || request.socket->state() != QAbstractSocket::ConnectedState) return;
        request.socket->write("HTTP/1.1 "+QByteArray::number(status)+" Fixture\r\nContent-Type: "+type+
            "\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
        request.socket->disconnectFromHost();
    }
    static void jsonReply(const Request &request, const QJsonObject &object) {
        reply(request,200,QJsonDocument(object).toJson(QJsonDocument::Compact));
    }
private:
    QTcpServer server;
};
