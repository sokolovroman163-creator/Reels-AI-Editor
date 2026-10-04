#pragma once
#include <QByteArray>
#include <QString>

// Secret values are deliberately not QObject properties or QML invokables.
class AiSecretStore {
public:
    ~AiSecretStore();
    bool hasKey() const;
    bool save(const QString &key);
    bool remove();
    QByteArray readForRequest() const;
private:
    QByteArray m_sessionKey; // Desktop: memory only, never QSettings.
    bool m_removed = false;
};
