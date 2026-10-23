#include "core/DriveList.h"

#include <QTest>

namespace {

MountInfo mount(const char *path, const char *device, const char *fs = "ext4", const char *label = "")
{
    MountInfo m;
    m.rootPath = QString::fromUtf8(path);
    m.device = QString::fromUtf8(device);
    m.fsType = QString::fromUtf8(fs);
    m.label = QString::fromUtf8(label);
    return m;
}

QStringList names(const QList<DriveEntry> &list)
{
    QStringList out;
    for (const DriveEntry &e : list)
        out << e.name;
    return out;
}

} // namespace

class TestDrives : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init() { qputenv("USER", "deck"); }

    void steamDeckMounts()
    {
        // A Deck with a labeled SD card and a USB stick plugged in.
        const QList<MountInfo> mounts = {
            mount("/", "/dev/nvme0n1p4", "btrfs"),
            mount("/home", "/dev/nvme0n1p8"),
            mount("/var", "/dev/nvme0n1p6"),
            mount("/run", "tmpfs", "tmpfs"),
            mount("/run/media/deck/Games", "/dev/mmcblk0p1", "ext4", "Games"),
            mount("/run/media/deck/KINGSTON", "/dev/sda1", "vfat", "KINGSTON"),
        };
        const auto drives = DriveList::build(mounts, {}, QStringLiteral("/home/deck"), false);
        QCOMPARE(names(drives), (QStringList{QStringLiteral("Root"), QStringLiteral("Home"), QStringLiteral("Games"),
                                             QStringLiteral("KINGSTON")}));
        QCOMPARE(drives.at(0).path, QStringLiteral("/"));
        QCOMPARE(drives.at(1).path, QStringLiteral("/home/deck"));
        QCOMPARE(drives.at(2).kind, DriveEntry::Kind::SdCard);
        QCOMPARE(drives.at(3).kind, DriveEntry::Kind::Usb);
    }

    void unlabeledCardGetsAFriendlyName()
    {
        const QList<MountInfo> mounts = {mount("/run/media/mmcblk0p1", "/dev/mmcblk0p1"),
                                         mount("/run/media/deck/0a1b2c3d-9f8e-4d7c-b6a5-112233445566", "/dev/sdb1")};
        const auto drives = DriveList::build(mounts, {}, QStringLiteral("/home/deck"), false);
        QCOMPARE(drives.at(2).name, QStringLiteral("SD Card"));
        QCOMPARE(drives.at(3).name, QStringLiteral("USB Drive"));
    }

    void flatpakFallbackFolders()
    {
        // The sandbox sees no mounts, only the shared folders.
        const QStringList folders = {QStringLiteral("/run/media/deck/SteamLibrary"), QStringLiteral("/run/media/mmcblk0p1")};
        const auto drives = DriveList::build({}, folders, QStringLiteral("/home/deck"), false);
        QCOMPARE(drives.size(), 4);
        QCOMPARE(drives.at(2).kind, DriveEntry::Kind::SdCard);
        QCOMPARE(drives.at(3).name, QStringLiteral("SteamLibrary"));
    }

    void noDuplicatesBetweenMountsAndFolders()
    {
        const QList<MountInfo> mounts = {mount("/run/media/deck/Games", "/dev/mmcblk0p1", "ext4", "Games")};
        const QStringList folders = {QStringLiteral("/run/media/deck/Games"), QStringLiteral("/run/media/deck")};
        const auto drives = DriveList::build(mounts, folders, QStringLiteral("/home/deck"), false);
        QCOMPARE(drives.size(), 3);
    }

    void mntDisksCount()
    {
        const QList<MountInfo> mounts = {mount("/mnt/backup", "/dev/nvme1n1p1", "ext4", "Backup"),
                                         mount("/mnt", "/dev/nvme0n1p1")};
        const auto drives = DriveList::build(mounts, {}, QStringLiteral("/home/deck"), false);
        QCOMPARE(names(drives).last(), QStringLiteral("Backup"));
        QCOMPARE(drives.size(), 3);
    }

    void deepestMatchWins()
    {
        const QList<MountInfo> mounts = {mount("/run/media/deck/Games", "/dev/mmcblk0p1", "ext4", "Games")};
        const auto drives = DriveList::build(mounts, {}, QStringLiteral("/home/deck"), false);
        QCOMPARE(DriveList::indexContaining(drives, QStringLiteral("/home/deck/Music")), 1);
        QCOMPARE(DriveList::indexContaining(drives, QStringLiteral("/run/media/deck/Games/roms")), 2);
        QCOMPARE(DriveList::indexContaining(drives, QStringLiteral("/usr/share")), 0);
    }

    void scanRunsHere()
    {
        const auto drives = DriveList::scan();
        QVERIFY(drives.size() >= 2);
        QCOMPARE(drives.at(0).kind, DriveEntry::Kind::Root);
        QVERIFY(drives.at(0).bytesTotal > 0);
    }
};

QTEST_GUILESS_MAIN(TestDrives)
#include "tst_drives.moc"
