#include "PolzaProvider.h"
#include "AiSecretStore.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <algorithm>

PolzaProvider::PolzaProvider(AiSecretStore *secrets, QObject *parent, const QUrl &base, int deadlineMs)
    : AiProvider(parent), m_secrets(secrets), m_base(base), m_deadlineMs(qBound(50,deadlineMs,90000)) {
    // Alternate endpoints exist only for loopback test fixtures, never a user preference.
    if (base != QUrl(QStringLiteral("https://polza.ai/api/v1/"))
        && !(base.scheme() == QLatin1String("http") && base.host() == QLatin1String("127.0.0.1")))
        m_base = QUrl(QStringLiteral("https://polza.ai/api/v1/"));
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { m_timedOut = true; if (m_reply) m_reply->abort(); });
}
quint64 PolzaProvider::request(const QString &resource, const QJsonObject &payload, bool stream) {
    cancel();
    m_id = ++m_serial; m_resource = resource; m_payload = payload;
    m_stream = stream && resource == QLatin1String("chat/completions"); m_attempt = 0;
    const auto id = m_id;
    QTimer::singleShot(0, this, [this, id] { if (m_id == id) send(); });
    return id;
}
void PolzaProvider::cancel() {
    m_id = 0; m_timeout.stop();
    if (m_reply) { auto reply = m_reply; m_reply = nullptr; reply->disconnect(this); reply->abort(); reply->deleteLater(); }
    m_body.clear(); m_sse.clear(); m_content.clear(); m_calls.clear(); m_payload = {};
}
void PolzaProvider::send() {
    if (m_resource != QLatin1String("models") && m_resource != QLatin1String("key")
        && m_resource != QLatin1String("chat/completions")) { reject(QStringLiteral("bad_request")); return; }
    m_body.clear(); m_sse.clear(); m_content.clear(); m_calls.clear();
    m_done = false; m_timedOut = false; m_eventError = false; m_bytes = 0;
    QNetworkRequest req(m_base.resolved(QUrl(m_resource)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    req.setTransferTimeout(30000);
    req.setRawHeader("Accept", m_stream ? "text/event-stream" : "application/json");
    req.setRawHeader("Accept-Language", "ru");
    if (m_resource != QLatin1String("models")) {
        QByteArray key = m_secrets->readForRequest();
        if (key.isEmpty()) { reject(QStringLiteral("key_required")); return; }
        req.setRawHeader("Authorization", QByteArray("Bearer ") + key);
        key.fill('\0'); key.clear();
    }
    if (m_resource == QLatin1String("chat/completions")) {
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        auto body = m_payload; body.insert(QStringLiteral("stream"), m_stream);
        m_reply = m_network.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    } else m_reply = m_network.get(req);
    connect(m_reply, &QIODevice::readyRead, this, &PolzaProvider::receive);
    connect(m_reply, &QNetworkReply::finished, this, &PolzaProvider::finish);
    m_timeout.start(m_deadlineMs);
    emit diagnostic(QStringLiteral("Polza %1 attempt %2").arg(m_resource).arg(m_attempt + 1));
}
void PolzaProvider::receive() {
    if (!m_reply || m_eventError) return;
    const QByteArray bytes = m_reply->readAll();
    m_bytes += bytes.size();
    if (m_bytes > 8 * 1024 * 1024) { m_eventError = true; m_reply->abort(); return; }
    const bool sse = m_stream && m_reply->header(QNetworkRequest::ContentTypeHeader).toString().startsWith(QLatin1String("text/event-stream"));
    if (!sse) { m_body += bytes; return; }
    m_sse += bytes; m_sse.replace("\r\n", "\n");
    int end;
    while ((end = m_sse.indexOf("\n\n")) >= 0) {
        const QByteArray event = m_sse.left(end); m_sse.remove(0, end + 2); parseEvent(event);
    }
}
void PolzaProvider::parseEvent(const QByteArray &event) {
    QByteArray data;
    for (const QByteArray &line : event.split('\n')) if (line.startsWith("data:")) {
        if (!data.isEmpty()) data += '\n'; data += line.mid(5).trimmed();
    }
    if (data.isEmpty()) return;
    if (data == "[DONE]") { m_done = true; return; }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject() || doc.object().contains(QStringLiteral("error"))) { m_eventError = true; return; }
    const auto choices = doc.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) return; // Usage-only chunk.
    const auto delta = choices[0].toObject().value(QStringLiteral("delta")).toObject();
    const QString text = delta.value(QStringLiteral("content")).toString();
    m_content += text;
    if (!text.isEmpty()) emit textDelta(m_id, text);
    for (const QJsonValue &v : delta.value(QStringLiteral("tool_calls")).toArray()) {
        const auto part = v.toObject(); const int index = part.value(QStringLiteral("index")).toInt(-1);
        if (index < 0 || index >= 64) { m_eventError = true; continue; }
        auto call = m_calls.value(index);
        if (part.contains(QStringLiteral("id"))) call.insert(QStringLiteral("id"), part.value(QStringLiteral("id")));
        call.insert(QStringLiteral("type"), QStringLiteral("function"));
        auto fn = call.value(QStringLiteral("function")).toObject(); const auto next = part.value(QStringLiteral("function")).toObject();
        for (const QString &key : {QStringLiteral("name"), QStringLiteral("arguments")})
            if (next.contains(key)) fn.insert(key, fn.value(key).toString() + next.value(key).toString());
        call.insert(QStringLiteral("function"), fn); m_calls.insert(index, call);
    }
}
void PolzaProvider::finish() {
    if (!m_reply) return;
    receive(); m_timeout.stop();
    auto reply = m_reply; m_reply = nullptr;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto netError = reply->error();
    const bool sse = m_stream && reply->header(QNetworkRequest::ContentTypeHeader).toString().startsWith(QLatin1String("text/event-stream"));
    reply->deleteLater();
    emit diagnostic(QStringLiteral("Polza HTTP %1; %2 bytes").arg(status).arg(m_bytes));
    // Never log headers, payload, error body, private paths, or decrypted secrets.
    if ((status == 429 || status >= 500) && m_attempt < 2 && !(sse && m_bytes > 0)) {
        ++m_attempt; const auto id = m_id;
        QTimer::singleShot(1000 * (1 << m_attempt), this, [this, id] { if (m_id == id) send(); }); return;
    }
    if (m_timedOut || netError == QNetworkReply::TimeoutError) { reject(QStringLiteral("timeout")); return; }
    if (status < 200 || status >= 300) { reject(status ? errorCode(status, m_body) : QStringLiteral("offline")); return; }
    if (netError != QNetworkReply::NoError || m_eventError) { reject(QStringLiteral("malformed")); return; }
    QJsonObject result;
    if (sse) {
        if (!m_done || !m_sse.trimmed().isEmpty()) { reject(QStringLiteral("malformed")); return; }
        QJsonArray calls; for (const auto &call : m_calls) calls.append(call);
        QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), m_content}};
        if (!calls.isEmpty()) message.insert(QStringLiteral("tool_calls"), calls);
        result = {{QStringLiteral("choices"), QJsonArray{QJsonObject{{QStringLiteral("message"), message}}}}};
    } else {
        QJsonParseError error; const auto doc = QJsonDocument::fromJson(m_body, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject() || doc.object().contains(QStringLiteral("error"))) { reject(QStringLiteral("malformed")); return; }
        result = doc.object();
    }
    const auto id = m_id; m_id = 0; m_payload = {}; m_body.clear(); m_sse.clear(); m_content.clear(); m_calls.clear();
    emit completed(id, result);
}
void PolzaProvider::reject(const QString &code) {
    const auto id = m_id; cancel(); emit failed(id, code, errorMessage(code));
}
QString PolzaProvider::errorCode(int status, const QByteArray &body) {
    if (status == 401) return QStringLiteral("unauthorized");
    if (status == 402) return QStringLiteral("balance");
    if (status == 403) return QStringLiteral("forbidden");
    if (status == 429) return QStringLiteral("rate_limit");
    if (status >= 500) return QStringLiteral("server");
    if (status == 400 || status == 404) {
        const auto error = QJsonDocument::fromJson(body).object().value(QStringLiteral("error")).toObject();
        const QString text = (error.value(QStringLiteral("code")).toString() + QLatin1Char(' ') + error.value(QStringLiteral("message")).toString()).toLower();
        if (text.contains(QLatin1String("tool")) && (text.contains(QLatin1String("unsupported")) || text.contains(QLatin1String("not supported")))) return QStringLiteral("unsupported_tools");
        if (text.contains(QLatin1String("model")) && (text.contains(QLatin1String("not found")) || text.contains(QLatin1String("unavailable")) || text.contains(QLatin1String("model_not_found")))) return QStringLiteral("model_unavailable");
    }
    return QStringLiteral("bad_request");
}
QString PolzaProvider::errorMessage(const QString &code) {
    if (code == QLatin1String("key_required")) return tr("Enter or save the Polza API key first.");
    if (code == QLatin1String("unauthorized")) return tr("Invalid API key");
    if (code == QLatin1String("balance")) return tr("Insufficient funds");
    if (code == QLatin1String("forbidden")) return tr("This API key does not have access.");
    if (code == QLatin1String("rate_limit")) return tr("Request limit exceeded");
    if (code == QLatin1String("server")) return tr("Service temporarily unavailable");
    if (code == QLatin1String("offline")) return tr("No internet connection");
    if (code == QLatin1String("timeout")) return tr("The request timed out. Try again.");
    if (code == QLatin1String("malformed")) return tr("The provider returned an incomplete or invalid response.");
    if (code == QLatin1String("unsupported_tools")) return tr("This model does not support tool calling.");
    if (code == QLatin1String("model_unavailable")) return tr("This model is unavailable. Select another model in AI settings.");
    return tr("The provider rejected the request. Check the selected model and try again.");
}
