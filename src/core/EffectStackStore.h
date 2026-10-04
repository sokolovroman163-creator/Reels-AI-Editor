#pragma once

#include "Effect.h"
#include "Time.h"

#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QString>

#include <optional>

namespace drift {

// A clip's effect stack, lifted off the clip. `sourceDurationUs` is the timeline duration of the
// clip it came from: applying it to a clip of a different length rescales every keyframe by the
// ratio, so a fade authored over two seconds stays a fade over the whole of a six-second shot.
// A copy of a single effect is just a stack of length one.
struct EffectStackPreset
{
    QString id;
    QString label;
    TimeUs sourceDurationUs = 0;
    QList<Effect> effects;
    QList<Effect> audioEffects;

    bool isEmpty() const { return effects.isEmpty() && audioEffects.isEmpty(); }
};

// One wire format doing three jobs: the system-clipboard payload, an exported .drifteffects file,
// and each entry in the library. `id` is written only when non-empty, so an export carries none.
QJsonObject effectStackToJson(const EffectStackPreset &preset);

// Returns an isEmpty() preset for anything that is not a Drift effect stack. That marker check is
// what makes it safe to auto-detect a paste off the system clipboard, which is shared with every
// other application and usually holds prose.
EffectStackPreset effectStackFromJson(const QJsonObject &object);

// Ids of user-saved presets carry a prefix so they can never collide with a built-in template id.
bool isUserEffectPresetId(const QString &id);

// Effect stacks the user saved from the inspector or the timeline menu. Persisted per-user (not
// per-project) as a single JSON file, because the whole library is a handful of small objects and
// every mutation rewrites it anyway.
class EffectStackStore
{
public:
    static EffectStackStore &instance();

    // By value: the library is read from QML while the GUI thread saves and deletes, so nothing
    // may hand out a pointer into it.
    QList<EffectStackPreset> presets() const;
    std::optional<EffectStackPreset> presetForId(const QString &id) const;

    // Returns the minted id, or an empty string when the label is blank or the stack is empty.
    QString add(const QString &label, const EffectStackPreset &stack);
    bool rename(const QString &id, const QString &label);
    bool remove(const QString &id);

    bool exportToFile(const QString &id, const QString &path) const;
    // Always mints a fresh id, so re-importing your own export adds a copy instead of clobbering.
    QString importFromFile(const QString &path);

    // Test seam: drops the in-memory copy so the next read re-reads the file.
    void reload();

    static QString storePath();

private:
    EffectStackStore() = default;

    void ensureLoaded() const;  // call with m_mutex held
    bool save() const;          // call with m_mutex held

    mutable QMutex m_mutex;
    mutable QList<EffectStackPreset> m_presets;
    mutable bool m_loaded = false;
};

} // namespace drift
