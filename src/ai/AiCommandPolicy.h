#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

class AiCommandPolicy {
public:
    static bool allowed(const QString &tool);
    static bool mutates(const QString &tool);
    static QJsonObject schema(const QString &tool);
    static QJsonObject validate(const QString &tool, QJsonObject &args);
    static QJsonObject validatePlan(QJsonArray &plan);
    static QJsonValue redact(const QJsonValue &value);
};
