#include "AiSecretStore.h"
#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QCoreApplication>
#include <QtCore/qnativeinterface.h>
namespace {
constexpr auto kHelper = "org/cutwire/drift/PolzaKeyStore";
QJniObject context() { return QNativeInterface::QAndroidApplication::context(); }
}
#endif

AiSecretStore::~AiSecretStore() { m_sessionKey.fill('\0'); }
bool AiSecretStore::hasKey() const {
    if (m_removed) return false;
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticMethod<jboolean>(kHelper, "has", "(Landroid/content/Context;)Z", context().object());
#else
    return !m_sessionKey.isEmpty();
#endif
}
bool AiSecretStore::save(const QString &value) {
    const QString key = value.trimmed();
    if (key.isEmpty() || key.size() > 4096 || key.contains('\n') || key.contains('\r')) return false;
#ifdef Q_OS_ANDROID
    const bool result = QJniObject::callStaticMethod<jboolean>(kHelper, "save", "(Landroid/content/Context;Ljava/lang/String;)Z",
        context().object(), QJniObject::fromString(key).object());
#else
    m_sessionKey.fill('\0');
    m_sessionKey = key.toUtf8();
    const bool result = true;
#endif
    if (result) m_removed = false;
    return result;
}
bool AiSecretStore::remove() {
    m_removed = true; // Disable further requests even if storage deletion fails.
    m_sessionKey.fill('\0'); m_sessionKey.clear();
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticMethod<jboolean>(kHelper, "delete", "(Landroid/content/Context;)Z", context().object());
#else
    return true;
#endif
}
QByteArray AiSecretStore::readForRequest() const {
    if (m_removed) return {};
#ifdef Q_OS_ANDROID
    const QJniObject bytes = QJniObject::callStaticObjectMethod(kHelper, "read", "(Landroid/content/Context;)[B", context().object());
    if (!bytes.isValid()) return {};
    QJniEnvironment env;
    const jbyteArray array = bytes.object<jbyteArray>();
    const int size = env->GetArrayLength(array);
    if (size <= 0 || size > 4096) return {};
    QByteArray result(size, Qt::Uninitialized);
    env->GetByteArrayRegion(array, 0, size, reinterpret_cast<jbyte *>(result.data()));
    QByteArray zeros(size, '\0');
    env->SetByteArrayRegion(array, 0, size, reinterpret_cast<const jbyte *>(zeros.constData()));
    return result;
#else
    return m_sessionKey;
#endif
}
