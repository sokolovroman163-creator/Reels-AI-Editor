#include "EffectStackStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace drift {

namespace {

const QString kIdPrefix = QStringLiteral("user:");
const QString kMarkerKey = QStringLiteral("drift");
const QString kMarkerValue = QStringLiteral("effectStack");
constexpr int kPayloadVersion = 1;
constexpr int kLibraryVersion = 1;

QString mintId()
{
    return kIdPrefix + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace

QJsonObject effectStackToJson(const EffectStackPreset &preset)
{
    QJsonObject object{
        {kMarkerKey, kMarkerValue},
        {QStringLiteral("version"), kPayloadVersion},
        {QStringLiteral("label"), preset.label},
        {QStringLiteral("sourceDurationUs"), static_cast<double>(preset.sourceDurationUs)},
        {QStringLiteral("effects"), effectsToJson(preset.effects)},
        {QStringLiteral("audioEffects"), effectsToJson(preset.audioEffects)},
    };
    if (!preset.id.isEmpty())
        object.insert(QStringLiteral("id"), preset.id);
    return object;
}

EffectStackPreset effectStackFromJson(const QJsonObject &object)
{
    EffectStackPreset preset;
    if (object.value(kMarkerKey).toString() != kMarkerValue)
        return preset; // not ours: ordinary clipboard text, or an unrelated JSON file
    // A payload from a future version may carry fields this build would silently drop, and
    // pasting half a stack is worse than pasting none.
    if (object.value(QStringLiteral("version")).toInt(kPayloadVersion) > kPayloadVersion)
        return preset;

    preset.id = object.value(QStringLiteral("id")).toString();
    preset.label = object.value(QStringLiteral("label")).toString();
    preset.sourceDurationUs =
        static_cast<TimeUs>(object.value(QStringLiteral("sourceDurationUs")).toDouble());
    preset.effects = effectsFromJson(object.value(QStringLiteral("effects")).toArray());
    preset.audioEffects = effectsFromJson(object.value(QStringLiteral("audioEffects")).toArray());
    return preset;
}

bool isUserEffectPresetId(const QString &id)
{
    return id.startsWith(kIdPrefix);
}

EffectStackStore &EffectStackStore::instance()
{
    static EffectStackStore store;
    return store;
}

QString EffectStackStore::storePath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return {};
    return QDir(base).filePath(QStringLiteral("effect-presets.json"));
}

void EffectStackStore::ensureLoaded() const
{
    if (m_loaded)
        return;
    m_loaded = true;
    m_presets.clear();

    const QString path = storePath();
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return; // no library yet

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue &value : root.value(QStringLiteral("presets")).toArray()) {
        const EffectStackPreset preset = effectStackFromJson(value.toObject());
        if (preset.id.isEmpty() || preset.label.isEmpty() || preset.isEmpty())
            continue;
        m_presets.append(preset);
    }
}

bool EffectStackStore::save() const
{
    const QString path = storePath();
    if (path.isEmpty())
        return false;

    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonArray presets;
    for (const EffectStackPreset &preset : m_presets)
        presets.append(effectStackToJson(preset));

    const QJsonObject root{
        {QStringLiteral("version"), kLibraryVersion},
        {QStringLiteral("presets"), presets},
    };

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

QList<EffectStackPreset> EffectStackStore::presets() const
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    return m_presets;
}

std::optional<EffectStackPreset> EffectStackStore::presetForId(const QString &id) const
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (const EffectStackPreset &preset : m_presets) {
        if (preset.id == id)
            return preset;
    }
    return std::nullopt;
}

QString EffectStackStore::add(const QString &label, const EffectStackPreset &stack)
{
    const QString trimmed = label.trimmed();
    if (trimmed.isEmpty() || stack.isEmpty())
        return {};

    QMutexLocker locker(&m_mutex);
    ensureLoaded();

    EffectStackPreset preset = stack;
    preset.id = mintId();
    preset.label = trimmed;

    m_presets.prepend(preset);
    if (!save()) {
        m_presets.removeFirst();
        return {};
    }
    return preset.id;
}

bool EffectStackStore::rename(const QString &id, const QString &label)
{
    const QString trimmed = label.trimmed();
    if (trimmed.isEmpty())
        return false;

    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (EffectStackPreset &preset : m_presets) {
        if (preset.id != id)
            continue;
        const QString previous = preset.label;
        preset.label = trimmed;
        if (!save()) {
            preset.label = previous;
            return false;
        }
        return true;
    }
    return false;
}

bool EffectStackStore::remove(const QString &id)
{
    QMutexLocker locker(&m_mutex);
    ensureLoaded();
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets.at(i).id != id)
            continue;
        const EffectStackPreset removed = m_presets.takeAt(i);
        if (!save()) {
            m_presets.insert(i, removed);
            return false;
        }
        return true;
    }
    return false;
}

bool EffectStackStore::exportToFile(const QString &id, const QString &path) const
{
    const std::optional<EffectStackPreset> preset = presetForId(id);
    if (!preset)
        return false;

    EffectStackPreset out = *preset;
    // An export is a payload like any other; carrying the library id would only invite an
    // importer to try to honour it.
    out.id.clear();

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(effectStackToJson(out)).toJson(QJsonDocument::Indented));
    return file.commit();
}

QString EffectStackStore::importFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    EffectStackPreset incoming =
        effectStackFromJson(QJsonDocument::fromJson(file.readAll()).object());
    if (incoming.isEmpty())
        return {};

    const QString label =
        incoming.label.isEmpty() ? QFileInfo(path).completeBaseName() : incoming.label;
    return add(label, incoming);
}

void EffectStackStore::reload()
{
    QMutexLocker locker(&m_mutex);
    m_loaded = false;
    m_presets.clear();
}

} // namespace drift
