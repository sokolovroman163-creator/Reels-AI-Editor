#include "McpAiBridge.h"
#include "AiCommandPolicy.h"
#include "mcp/McpCatalog.h"
#include "mcp/McpJson.h"

McpAiBridge::McpAiBridge(AppController *controller) : m_dispatcher(controller) { }
QJsonObject McpAiBridge::briefCatalog() const {
    auto result = drift::mcp::catalogPayload({{QStringLiteral("brief"), true}});
    QJsonArray boxes;
    for (const auto &v : result.value(QStringLiteral("toolboxes")).toArray()) {
        auto box = v.toObject(); QJsonArray ops;
        for (const auto &op : box.value(QStringLiteral("ops")).toArray()) if (AiCommandPolicy::allowed(op.toString())) ops.append(op);
        if (ops.isEmpty()) continue;
        box.insert(QStringLiteral("ops"), ops); boxes.append(box);
    }
    result.insert(QStringLiteral("toolboxes"), boxes); return result;
}
QJsonObject McpAiBridge::loadTools(const QString &box, const QStringList &ops) {
    auto result = drift::mcp::toolboxPayload(box, ops); QJsonArray allowed;
    for (const auto &v : result.value(QStringLiteral("tools")).toArray()) {
        const QString name = v.toObject().value(QStringLiteral("name")).toString();
        if (!AiCommandPolicy::allowed(name)) continue;
        allowed.append(v); m_loaded.removeAll(name); m_loaded.append(name);
    }
    while (m_loaded.size() > 16) m_loaded.removeFirst();
    result.insert(QStringLiteral("tools"), allowed); result.insert(QStringLiteral("n"), allowed.size()); return result;
}
QJsonArray McpAiBridge::tools() const {
    QJsonArray result = drift::mcp::homepageTools();
    for (const auto &v : drift::mcp::toolboxPayload({}, m_loaded).value(QStringLiteral("tools")).toArray()) result.append(v);
    return result;
}
QJsonObject McpAiBridge::call(const QString &tool, QJsonObject args) {
    const auto valid = AiCommandPolicy::validate(tool, args);
    if (!valid.value(QStringLiteral("ok")).toBool()) return valid;
    if (tool == QLatin1String("catalog")) return briefCatalog();
    if (tool == QLatin1String("toolbox")) {
        QStringList ops; for (const auto &v : args.value(QStringLiteral("ops")).toArray()) ops.append(v.toString());
        return loadTools(args.value(QStringLiteral("name")).toString(), ops);
    }
    if (tool == QLatin1String("search")) {
        auto result = drift::mcp::searchOps(args.value(QStringLiteral("q")).toString(), 50, true); QJsonArray hits;
        for (const auto &v : result.value(QStringLiteral("hits")).toArray()) {
            const QString name = v.toObject().value(QStringLiteral("name")).toString();
            if (!AiCommandPolicy::allowed(name)) continue;
            auto row = v.toObject(); row.insert(QStringLiteral("inputSchema"), AiCommandPolicy::schema(name)); hits.append(row);
            loadTools({}, {name});
            if (hits.size() >= qBound(1, args.value(QStringLiteral("limit")).toInt(3), 3)) break;
        }
        result.insert(QStringLiteral("hits"), hits); return result;
    }
    if (tool == QLatin1String("inspect")) return m_dispatcher.inspect(args);
    if (tool == QLatin1String("frames")) return m_dispatcher.frames(args);
    if (tool == QLatin1String("capture")) return m_dispatcher.capture(args);
    if (tool == QLatin1String("activity")) return m_dispatcher.activity(args);
    if (tool == QLatin1String("apply")) return m_dispatcher.apply(args);
    return m_dispatcher.applyOne(tool, args);
}
