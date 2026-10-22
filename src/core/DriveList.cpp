#include "core/DriveList.h"

#include "core/FileOps.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>

namespace {

QString userName()
{
    QString name = qEnvironmentVariable("USER");
    if (name.isEmpty())
        name = QFileInfo(QDir::homePath()).fileName();
    return name;
}

bool isUnder(const QString &path, const QString &parent)
{
    return path.startsWith(parent + QLatin1Char('/'));
}

DriveEntry::Kind kindFor(const QString &device, const QString &path)
{
    const QString dev = QFileInfo(device).fileName();
    const QString leaf = QFileInfo(path).fileName();
    if (dev.startsWith(QLatin1String("mmcblk")) || leaf.startsWith(QLatin1String("mmcblk")))
        return DriveEntry::Kind::SdCard;
    if (dev.startsWith(QLatin1String("sd")))
        return DriveEntry::Kind::Usb;
    if (dev.startsWith(QLatin1String("nvme")))
        return DriveEntry::Kind::Disk;
    // Unknown device (the Flatpak fallback): anything under a removable-media
    // folder is most likely a stick or a card.
    if (path.startsWith(QLatin1String("/run/media/")) || path.startsWith(QLatin1String("/media/")))
        return DriveEntry::Kind::Usb;
    return DriveEntry::Kind::Disk;
}

QString nameFor(DriveEntry::Kind kind, const QString &label, const QString &path)
{
    if (!label.isEmpty())
        return label;
    const QString leaf = QFileInfo(path).fileName();
    // SteamOS mounts an unlabelled card as /run/media/mmcblk0p1 or by UUID.
    const bool looksGenerated = leaf.startsWith(QLatin1String("mmcblk")) || leaf.startsWith(QLatin1String("sd"))
        || (leaf.size() >= 8 && leaf.count(QLatin1Char('-')) >= 1 && leaf.at(0).isLetterOrNumber()
            && leaf.toLower() == leaf && leaf.contains(QRegularExpression(QStringLiteral("^[0-9a-f-]+$"))));
    if (kind == DriveEntry::Kind::SdCard && (leaf.isEmpty() || looksGenerated))
        return QStringLiteral("SD Card");
    if (kind == DriveEntry::Kind::Usb && (leaf.isEmpty() || looksGenerated))
        return QStringLiteral("USB Drive");
    return leaf.isEmpty() ? path : leaf;
}

} // namespace

namespace DriveList {

QStringList mountParents(const QString &user)
{
    return {QStringLiteral("/run/media/") + user, QStringLiteral("/run/media"), QStringLiteral("/media/") + user,
            QStringLiteral("/media"), QStringLiteral("/mnt")};
}

QList<MountInfo> currentMounts()
{
    QList<MountInfo> out;
    const auto volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &v : volumes) {
        if (!v.isValid())
            continue;
        MountInfo m;
        m.rootPath = v.rootPath();
        m.device = QString::fromUtf8(v.device());
        m.fsType = QString::fromUtf8(v.fileSystemType());
        m.label = v.name();
        out << m;
    }
    return out;
}

QList<DriveEntry> build(const QList<MountInfo> &mounts, const QStringList &folders, const QString &home, bool withSpace)
{
    QList<DriveEntry> out;

    DriveEntry root;
    root.kind = DriveEntry::Kind::Root;
    root.name = QStringLiteral("Root");
    root.path = QStringLiteral("/");
    out << root;

    DriveEntry homeEntry;
    homeEntry.kind = DriveEntry::Kind::Home;
    homeEntry.name = QStringLiteral("Home");
    homeEntry.path = QDir::cleanPath(home);
    out << homeEntry;

    const QStringList parents = mountParents(userName());
    auto underAParent = [&](const QString &p) {
        for (const QString &parent : parents) {
            if (isUnder(p, parent))
                return true;
        }
        return false;
    };
    static const QSet<QString> pseudo = {
        QStringLiteral("tmpfs"), QStringLiteral("proc"), QStringLiteral("sysfs"), QStringLiteral("devtmpfs"),
        QStringLiteral("cgroup"), QStringLiteral("cgroup2"), QStringLiteral("overlay"), QStringLiteral("squashfs"),
        QStringLiteral("autofs"), QStringLiteral("fusectl"), QStringLiteral("debugfs"), QStringLiteral("tracefs"),
    };

    QSet<QString> seen;
    QList<DriveEntry> external;
    for (const MountInfo &m : mounts) {
        const QString path = QDir::cleanPath(m.rootPath);
        if (seen.contains(path) || !underAParent(path) || pseudo.contains(m.fsType))
            continue;
        // A parent folder that is itself a mount (a bare /mnt disk) still counts;
        // only the parents themselves are skipped.
        if (parents.contains(path))
            continue;
        seen.insert(path);
        DriveEntry e;
        e.kind = kindFor(m.device, path);
        e.device = m.device;
        e.path = path;
        e.name = nameFor(e.kind, m.label, path);
        external << e;
    }

    // Folders in the mount parents that the mount table did not mention.
    for (const QString &raw : folders) {
        const QString path = QDir::cleanPath(raw);
        if (seen.contains(path) || parents.contains(path))
            continue;
        // /run/media/deck is a parent, not a drive; skip parents of known drives.
        bool isParentOfKnown = false;
        for (const QString &s : std::as_const(seen)) {
            if (isUnder(s, path)) {
                isParentOfKnown = true;
                break;
            }
        }
        if (isParentOfKnown)
            continue;
        seen.insert(path);
        DriveEntry e;
        e.kind = kindFor({}, path);
        e.path = path;
        e.name = nameFor(e.kind, {}, path);
        external << e;
    }

    std::sort(external.begin(), external.end(), [](const DriveEntry &a, const DriveEntry &b) {
        // Cards first (the Deck's own slot), then USB, then other disks.
        if (a.kind != b.kind)
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        return FileOps::naturalCompare(a.name, b.name) < 0;
    });
    out += external;

    if (withSpace) {
        for (DriveEntry &e : out) {
            QStorageInfo info(e.path);
            if (info.isValid() && info.isReady()) {
                e.bytesFree = info.bytesAvailable();
                e.bytesTotal = info.bytesTotal();
            }
        }
    }
    return out;
}

QList<DriveEntry> scan()
{
    const QString user = userName();
    QStringList folders;
    for (const QString &parent : mountParents(user)) {
        const QDir dir(parent);
        if (!dir.exists())
            continue;
        const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &e : entries) {
            const QString full = dir.filePath(e);
            // /run/media/<user> shows up inside /run/media; it is a parent.
            if (mountParents(user).contains(full))
                continue;
            // Empty folders under /mnt or /media are leftover mount points.
            if (QDir(full).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
                continue;
            folders << full;
        }
    }
    return build(currentMounts(), folders, QDir::homePath(), true);
}

int indexContaining(const QList<DriveEntry> &drives, const QString &path)
{
    const QString p = QDir::cleanPath(path);
    int best = -1;
    int bestLen = -1;
    for (int i = 0; i < drives.size(); ++i) {
        const QString d = drives.at(i).path;
        if (FileOps::isSameOrInside(p, d) && d.size() > bestLen) {
            best = i;
            bestLen = d.size();
        }
    }
    return best;
}

QString iconName(DriveEntry::Kind kind)
{
    switch (kind) {
    case DriveEntry::Kind::Root:
        return QStringLiteral("drive-harddisk-root");
    case DriveEntry::Kind::Home:
        return QStringLiteral("user-home");
    case DriveEntry::Kind::SdCard:
        return QStringLiteral("media-flash-sd-mmc");
    case DriveEntry::Kind::Usb:
        return QStringLiteral("drive-removable-media-usb");
    case DriveEntry::Kind::Disk:
        return QStringLiteral("drive-harddisk");
    }
    return QStringLiteral("drive-harddisk");
}

} // namespace DriveList
