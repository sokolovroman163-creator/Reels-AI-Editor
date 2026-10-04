#pragma once

#include <QString>

namespace drift {

// A folder in the media bin's hierarchy. Nesting is via parentId, like a filesystem —
// empty parentId means the folder lives at bin root.
struct BinFolder
{
    QString id;
    QString name;
    QString parentId;
};

} // namespace drift
