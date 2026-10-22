#include "core/FileItem.h"

#include <QFileInfo>

FileItem FileItem::fromFileInfo(const QFileInfo &info)
{
    FileItem item;
    item.name = info.fileName();
    item.path = info.absoluteFilePath();
    item.isSymlink = info.isSymLink();
    if (item.isSymlink)
        item.symlinkTarget = info.symLinkTarget();
    // A symlink to a folder behaves like a folder when browsing.
    item.isDir = info.isDir();
    item.size = item.isDir ? 0 : info.size();
    item.modified = info.lastModified();
    item.permissions = info.permissions();
    item.isHidden = item.name.startsWith(QLatin1Char('.'));
    item.isReadable = info.isReadable();
    return item;
}
