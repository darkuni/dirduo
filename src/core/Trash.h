#pragma once

#include <QString>

// Freedesktop.org trash, written directly instead of through QFile::moveToTrash
// because inside a Flatpak that call would trash into the app's private data
// folder. Files land where Dolphin, Nautilus and the desktop's trash can see
// them:
//   - same device as home: ~/.local/share/Trash
//   - other devices (SD card, USB): $topdir/.Trash/$uid if the admin made a
//     sticky .Trash, otherwise $topdir/.Trash-$uid
namespace Trash {

QString homeTrashDir();

// Moves path (file, folder or symlink) to the trash. trashedPath receives the
// new location under .../files/.
bool moveToTrash(const QString &path, QString *trashedPath, QString *error);

// Trash folder that moveToTrash would use for path, created if needed.
// Empty with error set when the device has no usable trash.
QString trashDirFor(const QString &path, QString *error);

} // namespace Trash
