#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// The entries of the Drives toolbar: the filesystem root, the home folder, and
// every external volume mounted under /run/media, /media or /mnt (SD cards,
// USB drives, extra disks).
struct DriveEntry
{
    enum class Kind { Root, Home, SdCard, Usb, Disk };

    Kind kind = Kind::Disk;
    QString name;   // label shown on the button
    QString path;   // where Go takes you
    QString device; // /dev/... when known
    qint64 bytesFree = -1;
    qint64 bytesTotal = -1;

    bool operator==(const DriveEntry &o) const
    {
        return kind == o.kind && name == o.name && path == o.path;
    }
};

// One mounted filesystem as the OS reports it; split out so tests can feed in
// a Steam Deck's mounts without having one.
struct MountInfo
{
    QString rootPath;
    QString device;
    QString fsType;
    QString label;
};

namespace DriveList {

// Mount points under which external volumes live.
QStringList mountParents(const QString &user);

// Builds the list from mounts plus folders found in the mount parents.
// Folders cover the Flatpak case, where the sandbox may not see the host's
// mount table even though /run/media is shared.
QList<DriveEntry> build(const QList<MountInfo> &mounts, const QStringList &mountParentFolders,
                        const QString &home, bool withSpace);

// The real thing: current mounts, current folders, with free space.
QList<DriveEntry> scan();

QList<MountInfo> currentMounts();

// The entry whose path contains `path`, preferring the deepest match. -1 if none.
int indexContaining(const QList<DriveEntry> &drives, const QString &path);

QString iconName(DriveEntry::Kind kind);

} // namespace DriveList
