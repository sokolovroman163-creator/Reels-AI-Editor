#include "MulticamImageStore.h"

#include <QHash>
#include <QMutex>

namespace {

QMutex g_mutex;
QHash<int, QImage> g_tiles;

} // namespace

namespace MulticamImageStore {

void setTile(int angle, const QImage &frame)
{
    QMutexLocker lock(&g_mutex);
    if (frame.isNull())
        g_tiles.remove(angle);
    else
        g_tiles.insert(angle, frame);
}

void clear()
{
    QMutexLocker lock(&g_mutex);
    g_tiles.clear();
}

QImage tile(int angle)
{
    QMutexLocker lock(&g_mutex);
    return g_tiles.value(angle);
}

} // namespace MulticamImageStore
