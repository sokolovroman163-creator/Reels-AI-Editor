#pragma once

#include <QQuickImageProvider>

// Serves the multicam window's angle tiles from MulticamImageStore, as
// image://multicam/<angleIndex>. The pixels are decoded per playhead position and never hit
// disk, so there is nothing for DriftImageProvider's path-based lookup to load.
class MulticamImageProvider : public QQuickImageProvider
{
public:
    MulticamImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
