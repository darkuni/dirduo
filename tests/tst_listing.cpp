// Phase 2 core: file kinds, label rules, folder sizes and flat listings.
#include "core/DirLister.h"
#include "core/FileKind.h"
#include "core/FolderSizes.h"
#include "core/LabelRules.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

namespace {
void write(const QString &path, int bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(bytes, 'x'));
}
} // namespace

class TestListing : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void fileKinds()
    {
        using FileKind::Category;
        QCOMPARE(FileKind::categoryFor(QStringLiteral("Photo.JPG"), false), Category::Image);
        QCOMPARE(FileKind::categoryFor(QStringLiteral("game.d64"), false), Category::DiskImage);
        QCOMPARE(FileKind::categoryFor(QStringLiteral("song.flac"), false), Category::Audio);
        QCOMPARE(FileKind::categoryFor(QStringLiteral("whatever.jpg"), true), Category::Folder);
        QCOMPARE(FileKind::extensionOf(QStringLiteral("a.tar.gz")), QStringLiteral("gz"));
        for (int c = 0; c <= int(Category::Other); ++c) {
            Category back;
            QVERIFY(FileKind::categoryFromKey(FileKind::categoryKey(Category(c)), back));
            QCOMPARE(back, Category(c));
        }
    }

    void labelRules()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("labels.ini"));
        LabelRule music;
        music.pattern = QStringLiteral("*.mp3;*.flac");
        music.color = QColor(Qt::magenta);
        music.bold = true;
        music.applies = LabelRule::Applies::Files;
        LabelRule backup;
        backup.pattern = QStringLiteral("*backup*");
        backup.color = QColor(Qt::red);
        {
            LabelRules rules(file);
            rules.setRules({music, backup});
            QVERIFY(rules.save());
        }
        LabelRules loaded(file);
        loaded.load();
        QCOMPARE(loaded.rules().size(), 2);
        const LabelRule *m = loaded.match(QStringLiteral("Track.FLAC"), false);
        QVERIFY(m && m->bold && m->color == QColor(Qt::magenta));
        QVERIFY(!loaded.match(QStringLiteral("songs.mp3"), true) || loaded.match(QStringLiteral("songs.mp3"), true)->pattern != music.pattern);
        QVERIFY(loaded.match(QStringLiteral("old backup"), true));
        QVERIFY(!loaded.match(QStringLiteral("readme.txt"), false));
    }

    void folderSizesMeasure()
    {
        QTemporaryDir dir;
        write(dir.filePath(QStringLiteral("t/a.bin")), 1000);
        write(dir.filePath(QStringLiteral("t/sub/b.bin")), 24);
        QDir().mkpath(dir.filePath(QStringLiteral("t/empty")));
        // A hard link counts once; a symlink is not followed.
        QVERIFY(::link(QFile::encodeName(dir.filePath(QStringLiteral("t/a.bin"))).constData(),
                       QFile::encodeName(dir.filePath(QStringLiteral("t/sub/a-link.bin"))).constData()) == 0);
        QVERIFY(QFile::link(dir.filePath(QStringLiteral("t")), dir.filePath(QStringLiteral("t/sub/loop"))));
        const FolderSizes::Result r = FolderSizes::measure(dir.filePath(QStringLiteral("t")));
        QCOMPARE(r.bytes, qint64(1024));
        QVERIFY(r.complete);
        QVERIFY(FolderSizes::measure(dir.filePath(QStringLiteral("t/empty"))).isEmpty());

        FolderSizes sizes;
        QSignalSpy ready(&sizes, &FolderSizes::ready);
        sizes.request(dir.filePath(QStringLiteral("t/sub")));
        QTRY_VERIFY(sizes.has(dir.filePath(QStringLiteral("t/sub"))));
        QCOMPARE(sizes.result(dir.filePath(QStringLiteral("t/sub"))).bytes, qint64(1024));
        sizes.invalidate(dir.filePath(QStringLiteral("t/sub")));
        QVERIFY(!sizes.has(dir.filePath(QStringLiteral("t/sub"))));
    }

    void flatListing()
    {
        QTemporaryDir dir;
        write(dir.filePath(QStringLiteral("one.txt")), 1);
        write(dir.filePath(QStringLiteral("x/two.txt")), 1);
        write(dir.filePath(QStringLiteral("x/y/three.txt")), 1);
        bool truncated = true;
        QString error;
        FileItemList all = DirLister::listRecursive(dir.path(), true, 1000, &truncated, &error);
        QCOMPARE(all.size(), 5);
        QVERIFY(!truncated);
        bool found = false;
        for (const FileItem &it : all) {
            if (it.name == QLatin1String("three.txt")) {
                found = true;
                QCOMPARE(it.relPath, QStringLiteral("x/y"));
            }
        }
        QVERIFY(found);
        QCOMPARE(DirLister::listRecursive(dir.path(), false, 1000, &truncated, &error).size(), 3);
        QCOMPARE(DirLister::listRecursive(dir.path(), true, 2, &truncated, &error).size(), 2);
        QVERIFY(truncated);
    }
};

QTEST_GUILESS_MAIN(TestListing)
#include "tst_listing.moc"
