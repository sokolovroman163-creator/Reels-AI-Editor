#pragma once

#include <QImage>

// The multicam window's decoded angle tiles, held between the worker that decodes them and the
// QML image-loading thread that serves them. One entry per angle slot.
//
// Split out from MulticamImageProvider for the same reason SegmentImageStore is: the
// model-layer tests link Gui and Concurrent but not Quick, and AppController writes here.
namespace MulticamImageStore {

void setTile(int angle, const QImage &frame);
void clear();

QImage tile(int angle);

} // namespace MulticamImageStore
