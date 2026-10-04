#pragma once
#include "mcp/McpDispatcher.h"
#include <QJsonArray>
#include <QStringList>
class AppController;
class McpAiBridge {
public:
    explicit McpAiBridge(AppController *controller);
    QJsonObject call(const QString &tool, QJsonObject args);
    QJsonArray tools() const;
    QJsonObject briefCatalog() const;
private:
    QJsonObject loadTools(const QString &box, const QStringList &ops);
    drift::mcp::McpDispatcher m_dispatcher;
    QStringList m_loaded;
};
