#pragma once

#include <QString>
#include <QVariantMap>

// Snapshot of decoder/encoder capability and host facts for the Debug info
// dialog and GitHub bug reports. Hardware decode is VAAPI (Windows/macOS
// report none). Hardware encode is probed from the same NVENC/QSV/AMF/VAAPI/
// VideoToolbox catalog the exporter uses. `hints` lists missing Flatpak
// extensions and an ONNX Runtime install when those are absent.
class DebugReport
{
public:
    static QVariantMap collect();
    static QString formatPlainText(const QVariantMap &info);
};
