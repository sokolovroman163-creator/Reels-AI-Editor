#pragma once
#include "AiProvider.h"
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QMap>
class AiSecretStore;
class QNetworkReply;

class PolzaProvider final : public AiProvider {
    Q_OBJECT
public:
    explicit PolzaProvider(AiSecretStore *secrets, QObject *parent = nullptr,
                          const QUrl &base = QUrl(QStringLiteral("https://polza.ai/api/v1/")),
                          int deadlineMs = 90000);
    quint64 request(const QString &resource, const QJsonObject &payload = {}, bool stream = false) override;
    void cancel() override;
    static QString errorCode(int status, const QByteArray &body);
    static QString errorMessage(const QString &code);
private:
    void send();
    void receive();
    void finish();
    void parseEvent(const QByteArray &event);
    void reject(const QString &code);
    AiSecretStore *m_secrets;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    QUrl m_base;
    QString m_resource;
    QJsonObject m_payload;
    QByteArray m_body, m_sse;
    QString m_content;
    QMap<int, QJsonObject> m_calls;
    quint64 m_serial = 0, m_id = 0;
    int m_attempt = 0, m_bytes = 0;
    int m_deadlineMs;
    bool m_stream = false, m_done = false, m_timedOut = false, m_eventError = false;
};
