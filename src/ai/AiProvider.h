#pragma once
#include <QObject>
#include <QJsonObject>

class AiProvider : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual quint64 request(const QString &resource, const QJsonObject &payload = {}, bool stream = false) = 0;
    virtual void cancel() = 0;
signals:
    void completed(quint64 id, const QJsonObject &response);
    void failed(quint64 id, const QString &code, const QString &message);
    void textDelta(quint64 id, const QString &text);
    void diagnostic(const QString &line);
};
