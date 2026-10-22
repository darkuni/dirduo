// Synchronize (compare, default actions, plans, running) and the duplicate finder.
#include "core/AliasManager.h"
#include "core/CommandEngine.h"
#include "core/FolderSync.h"
#include "core/JobManager.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

namespace {

void write(const QString &path, const QByteArray &data, int ageSecs = 0)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
    f.close();
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-ageSecs), QFileDevice::FileModificationTime));
}

const SyncEntry *entry(const QList<SyncEntry> &list, const QString &rel)
{
    for (const SyncEntry &e : list) {
        if (e.rel == rel)
            return &e;
    }
    return nullptr;
}

} // namespace

class TestFolderSync : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        L = m_dir->filePath(QStringLiteral("internal"));
        R = m_dir->filePath(QStringLiteral("sdcard"));
        write(L + QStringLiteral("/same.d64"), "aaaa", 3600);
        write(R + QStringLiteral("/same.d64"), "aaaa", 3600);
        write(L + QStringLiteral("/newer-left.sav"), "new", 10);
        write(R + QStringLiteral("/newer-left.sav"), "old", 7200);
        write(L + QStringLiteral("/newer-right.sav"), "old", 7200);
        write(R + QStringLiteral("/newer-right.sav"), "new!", 10);
        write(L + QStringLiteral("/only-left/a.adf"), "x", 100);
        write(L + QStringLiteral("/only-left/b.adf"), "y", 100);
        write(R + QStringLiteral("/only-right.txt"), "z", 100);
        write(L + QStringLiteral("/sub/deep.rom"), "rom1", 500);
        write(R + QStringLiteral("/sub/deep.rom"), "rom2", 500); // same time and size, other bytes
        write(L + QStringLiteral("/.hidden"), "h", 100);
    }

    void compareStates()
    {
        SyncOptions o;
        o.includeHidden = false;
        const QList<SyncEntry> list = FolderSync::compare(L, R, o);
        QCOMPARE(entry(list, QStringLiteral("same.d64"))->state, SyncEntry::State::Same);
        QCOMPARE(entry(list, QStringLiteral("newer-left.sav"))->state, SyncEntry::State::LeftNewer);
        QCOMPARE(entry(list, QStringLiteral("newer-right.sav"))->state, SyncEntry::State::RightNewer);
        QCOMPARE(entry(list, QStringLiteral("only-left"))->state, SyncEntry::State::OnlyLeft);
        QVERIFY(entry(list, QStringLiteral("only-left"))->isDir);
        QVERIFY(!entry(list, QStringLiteral("only-left/a.adf"))); // goes with its folder
        QCOMPARE(entry(list, QStringLiteral("only-right.txt"))->state, SyncEntry::State::OnlyRight);
        // Same date and size: only a contents check tells them apart.
        QCOMPARE(entry(list, QStringLiteral("sub/deep.rom"))->state, SyncEntry::State::Same);
        QVERIFY(!entry(list, QStringLiteral(".hidden")));
        o.compareBy = SyncOptions::CompareBy::Contents;
        const QList<SyncEntry> byContents = FolderSync::compare(L, R, o);
        QCOMPARE(entry(byContents, QStringLiteral("sub/deep.rom"))->state, SyncEntry::State::Different);
        QCOMPARE(entry(byContents, QStringLiteral("same.d64"))->state, SyncEntry::State::Same);
        // Excluded names are left out on both sides.
        o.exclude = {QStringLiteral("*.sav")};
        QVERIFY(!entry(FolderSync::compare(L, R, o), QStringLiteral("newer-left.sav")));
    }

    void timeToleranceForSdCards()
    {
        write(L + QStringLiteral("/fat.txt"), "same", 1000);
        QFile::copy(L + QStringLiteral("/fat.txt"), R + QStringLiteral("/fat.txt"));
        QFile f(R + QStringLiteral("/fat.txt"));
        QVERIFY(f.open(QIODevice::ReadWrite));
        // FAT rounds to 2 seconds.
        QVERIFY(f.setFileTime(QFileInfo(L + QStringLiteral("/fat.txt")).lastModified().addSecs(1), QFileDevice::FileModificationTime));
        f.close();
        const QList<SyncEntry> list = FolderSync::compare(L, R, SyncOptions());
        QCOMPARE(entry(list, QStringLiteral("fat.txt"))->state, SyncEntry::State::Same);
    }

    void defaultActionsPerMode()
    {
        using A = SyncEntry::Action;
        using S = SyncEntry::State;
        using M = SyncOptions::Mode;
        QCOMPARE(FolderSync::defaultAction(S::OnlyRight, M::Update), A::None);
        QCOMPARE(FolderSync::defaultAction(S::OnlyRight, M::Mirror), A::DeleteRight);
        QCOMPARE(FolderSync::defaultAction(S::OnlyRight, M::TwoWay), A::CopyToLeft);
        QCOMPARE(FolderSync::defaultAction(S::RightNewer, M::Update), A::None);
        QCOMPARE(FolderSync::defaultAction(S::RightNewer, M::Mirror), A::CopyToRight);
        QCOMPARE(FolderSync::defaultAction(S::RightNewer, M::TwoWay), A::CopyToLeft);
        QCOMPARE(FolderSync::defaultAction(S::Different, M::TwoWay), A::None);
        QCOMPARE(FolderSync::defaultAction(S::Same, M::Mirror), A::None);
    }

    void twoWaySyncRuns()
    {
        SyncOptions o;
        o.mode = SyncOptions::Mode::TwoWay;
        o.compareBy = SyncOptions::CompareBy::Contents;
        const QList<SyncEntry> list = FolderSync::compare(L, R, o);
        const FolderSync::Plan p = FolderSync::plan(list, L, R);
        // newer-left ->, newer-right <-, only-left ->, only-right <-, .hidden ->; deep.rom is a conflict
        QCOMPARE(p.items, 5);
        AliasManager aliases(m_dir->filePath(QStringLiteral("aliases.ini")));
        JobManager jobs;
        CommandEngine engine(aliases, jobs);
        QVERIFY(FolderSync::run(p, engine) > 0);
        QVERIFY(jobs.waitForIdle(10000));
        QVERIFY(QFile::exists(R + QStringLiteral("/only-left/b.adf")));
        QVERIFY(QFile::exists(L + QStringLiteral("/only-right.txt")));
        QFile nl(R + QStringLiteral("/newer-left.sav"));
        QVERIFY(nl.open(QIODevice::ReadOnly));
        QCOMPARE(nl.readAll(), QByteArray("new"));
        QFile nr(L + QStringLiteral("/newer-right.sav"));
        QVERIFY(nr.open(QIODevice::ReadOnly));
        QCOMPARE(nr.readAll(), QByteArray("new!"));
        // Now only the conflict remains.
        const QList<SyncEntry> again = FolderSync::compare(L, R, o);
        int different = 0;
        for (const SyncEntry &e : again)
            different += e.state != SyncEntry::State::Same ? 1 : 0;
        QCOMPARE(different, 1);
    }

    void mirrorDeletesToTrash()
    {
        qputenv("XDG_DATA_HOME", QFile::encodeName(m_dir->filePath(QStringLiteral("data"))));
        SyncOptions o;
        o.mode = SyncOptions::Mode::Mirror;
        const FolderSync::Plan p = FolderSync::plan(FolderSync::compare(L, R, o), L, R);
        QCOMPARE(p.deletes, QStringList{R + QStringLiteral("/only-right.txt")});
        AliasManager aliases(m_dir->filePath(QStringLiteral("aliases.ini")));
        JobManager jobs;
        CommandEngine engine(aliases, jobs);
        FolderSync::run(p, engine);
        QVERIFY(jobs.waitForIdle(10000));
        QVERIFY(!QFile::exists(R + QStringLiteral("/only-right.txt")));
    }

    void duplicatesByContents()
    {
        const QString d = m_dir->filePath(QStringLiteral("roms"));
        QByteArray big(200 * 1024, 'g');
        write(d + QStringLiteral("/c64/Galaga.d64"), big);
        write(d + QStringLiteral("/backup/galaga (copy).d64"), big);
        write(d + QStringLiteral("/c64/elite.d64"), "elite");
        write(d + QStringLiteral("/other/elite-renamed.d64"), "elite");
        QByteArray almost = big;
        almost[150 * 1024] = 'h'; // same size and first 64 KB, different later
        write(d + QStringLiteral("/c64/galaga-hack.d64"), almost);
        write(d + QStringLiteral("/c64/empty1"), QByteArray());
        write(d + QStringLiteral("/c64/empty2"), QByteArray());
        // A hard link is the same file, not a duplicate.
        QVERIFY(::link(QFile::encodeName(d + QStringLiteral("/c64/elite.d64")).constData(),
                       QFile::encodeName(d + QStringLiteral("/c64/elite-link.d64")).constData()) == 0);

        ScanProgress progress;
        const QList<DuplicateGroup> groups = Duplicates::find({d, d + QStringLiteral("/c64")}, DuplicateOptions(), &progress);
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups.at(0).size, qint64(big.size())); // biggest waste first
        QCOMPARE(groups.at(0).paths.size(), 2);
        QVERIFY(!groups.at(0).paths.contains(d + QStringLiteral("/c64/galaga-hack.d64")));
        QCOMPARE(groups.at(1).paths.size(), 2);
        QCOMPARE(Duplicates::reclaimable(groups), qint64(big.size() + 5));
        QCOMPARE(progress.files.load(), 5); // overlapping roots and the hard link count once

        // By name: same name in two folders, different contents.
        write(d + QStringLiteral("/backup/GALAGA.D64"), "GALAGA");
        DuplicateOptions byName;
        byName.by = DuplicateOptions::By::Name;
        const QList<DuplicateGroup> names = Duplicates::find({d}, byName);
        QCOMPARE(names.size(), 1);
        QCOMPARE(names.first().paths.size(), 2);
        QVERIFY(Duplicates::find({d}, DuplicateOptions()).size() == 2); // contents still say otherwise
    }

    void cancelStops()
    {
        ScanProgress progress;
        progress.cancel = true;
        QVERIFY(Duplicates::find({m_dir->path()}, DuplicateOptions(), &progress).isEmpty());
        QVERIFY(FolderSync::compare(L, R, SyncOptions(), &progress).isEmpty());
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QString L;
    QString R;
};

QTEST_GUILESS_MAIN(TestFolderSync)
#include "tst_foldersync.moc"
