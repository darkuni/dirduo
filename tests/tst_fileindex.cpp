#include "core/FileIndex.h"
#include "core/Paths.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestFileIndex : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    QString root;

    void touch(const QString &rel, int bytes = 0)
    {
        const QString p = root + QLatin1Char('/') + rel;
        QDir().mkpath(QFileInfo(p).absolutePath());
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(bytes, 'x'));
    }
    QStringList names(const QString &query)
    {
        QStringList out;
        for (const FileIndex::Hit &h : FileIndex::instance()->search(query).hits)
            out << h.name;
        return out;
    }

private Q_SLOTS:
    void initTestCase()
    {
        Paths::setConfigDirOverride(m_dir.filePath(QStringLiteral("config")));
        Paths::setCacheHomeOverride(m_dir.filePath(QStringLiteral("cache")));
        root = m_dir.filePath(QStringLiteral("root"));
        touch(QStringLiteral("Emulation/roms/snes/Super Mario World (USA).sfc"), 512);
        touch(QStringLiteral("Emulation/roms/snes/Mario Kart (Europe).sfc"));
        touch(QStringLiteral("Emulation/roms/c64/Mario Bros.d64"));
        touch(QStringLiteral("Emulation/roms/c64/Sonic.prg"));
        touch(QStringLiteral("Music/Joan Jett/Bad Reputation.mp3"));
        touch(QStringLiteral("Music/notes.txt"));
        touch(QStringLiteral(".secret/hidden mario.txt"));
        touch(QStringLiteral("node_modules/mario.js"));
        FileIndex::Settings s;
        s.roots = {root};
        s.exclude = FileIndex::defaultExclude();
        s.rebuildMinutes = 0;
        FileIndex::instance()->setSettings(s); // starts: no cache yet, so it builds
        QVERIFY(FileIndex::instance()->waitIdle(10000));
        QTRY_VERIFY(FileIndex::instance()->isReady());
        QVERIFY(FileIndex::instance()->waitIdle(10000));
    }

    void searches()
    {
        QCOMPARE(names(QStringLiteral("mario")),
                 (QStringList{QStringLiteral("Mario Bros.d64"), QStringLiteral("Mario Kart (Europe).sfc"),
                              QStringLiteral("Super Mario World (USA).sfc")}));
        QCOMPARE(names(QStringLiteral("MARIO kart")), QStringList{QStringLiteral("Mario Kart (Europe).sfc")});
        QCOMPARE(names(QStringLiteral("*.d64")), QStringList{QStringLiteral("Mario Bros.d64")});
        QCOMPARE(names(QStringLiteral("ext:prg;d64")).size(), 2);
        QCOMPARE(names(QStringLiteral("folder: roms")), QStringList{QStringLiteral("roms")});
        QCOMPARE(names(QStringLiteral("file: snes")), QStringList());
        QCOMPARE(names(QStringLiteral("roms/c64")).size(), 3); // the folder and both files in it
        QCOMPARE(names(QStringLiteral("c64/son")), QStringList{QStringLiteral("Sonic.prg")});
        QCOMPARE(names(QStringLiteral("\"(usa)\"")), QStringList{QStringLiteral("Super Mario World (USA).sfc")});
        // Hidden folders and node_modules are left out.
        QVERIFY(!names(QStringLiteral("hidden")).contains(QStringLiteral("hidden mario.txt")));
        QVERIFY(!names(QStringLiteral("mario.js")).contains(QStringLiteral("mario.js")));
        const FileIndex::Result r = FileIndex::instance()->search(QStringLiteral("world"));
        QCOMPARE(r.total, 1);
        QCOMPARE(r.hits.first().size, 512);
        QCOMPARE(r.hits.first().path(), root + QStringLiteral("/Emulation/roms/snes/Super Mario World (USA).sfc"));
        QVERIFY(FileIndex::instance()->search(QString()).hits.isEmpty());
        // A limit keeps the count.
        const FileIndex::Result lim = FileIndex::instance()->search(QStringLiteral("o"), 2);
        QCOMPARE(lim.hits.size(), 2);
        QVERIFY(lim.total > 2);
        QVERIFY(lim.truncated);
    }

    void patches()
    {
        // A new file and a new folder with files in it.
        touch(QStringLiteral("Emulation/roms/c64/Zak McKracken.d64"));
        touch(QStringLiteral("Emulation/roms/gba/Mario Kart Super Circuit.gba"));
        FileIndex::instance()->rescan(root + QStringLiteral("/Emulation/roms"), true);
        FileIndex::instance()->rescan(root + QStringLiteral("/Emulation/roms/c64"), false);
        QTRY_VERIFY(names(QStringLiteral("kart")).contains(QStringLiteral("Mario Kart Super Circuit.gba")));
        QTRY_VERIFY(names(QStringLiteral("zak")).contains(QStringLiteral("Zak McKracken.d64")));
        // A folder that went: what was in it goes too.
        QVERIFY(QDir(root + QStringLiteral("/Emulation/roms/snes")).removeRecursively());
        FileIndex::instance()->rescan(root + QStringLiteral("/Emulation/roms"), false);
        QTRY_VERIFY(!names(QStringLiteral("world")).contains(QStringLiteral("Super Mario World (USA).sfc")));
        QVERIFY(names(QStringLiteral("kart")).contains(QStringLiteral("Mario Kart Super Circuit.gba")));
        QVERIFY(!names(QStringLiteral("kart")).contains(QStringLiteral("Mario Kart (Europe).sfc")));
        // A listed folder replaces its contents.
        FileItemList items;
        FileItem f;
        f.name = QStringLiteral("Bad Reputation.mp3");
        f.path = root + QStringLiteral("/Music/Joan Jett/") + f.name;
        items << f;
        f.name = QStringLiteral("Cherry Bomb.mp3");
        f.path = root + QStringLiteral("/Music/Joan Jett/") + f.name;
        items << f;
        FileIndex::instance()->folderListed(root + QStringLiteral("/Music/Joan Jett"), items);
        QCOMPARE(names(QStringLiteral("cherry")), QStringList{QStringLiteral("Cherry Bomb.mp3")});
        QCOMPARE(names(QStringLiteral("reputation")).size(), 1);
        // Folders outside the roots are not taken in.
        FileIndex::instance()->folderListed(QStringLiteral("/tmp"), items);
        QCOMPARE(names(QStringLiteral("cherry")).size(), 1);
    }

    void benchmark()
    {
        const QString where = qEnvironmentVariable("DIRDUO_BENCH");
        if (where.isEmpty())
            QSKIP("Set DIRDUO_BENCH to a big folder to time the index.");
        FileIndex::Settings s = FileIndex::instance()->settings();
        s.roots = {where};
        s.hidden = true;
        QElapsedTimer t;
        t.start();
        FileIndex::instance()->setSettings(s);
        QVERIFY(FileIndex::instance()->waitIdle(600000));
        qWarning() << "built" << FileIndex::instance()->count() << "in" << t.elapsed() << "ms";
        for (const char *q : {"lib", "python", "*.so", "ext:h;hpp", "include/qt", "zzzz"}) {
            t.restart();
            const FileIndex::Result r = FileIndex::instance()->search(QString::fromLatin1(q));
            qWarning() << q << r.total << "in" << t.elapsed() << "ms";
        }
        t.restart();
        FileIndex::instance()->start(); // load from the cache
        QVERIFY(FileIndex::instance()->waitIdle(60000));
        qWarning() << "loaded in" << t.elapsed() << "ms" << QFileInfo(FileIndex::instance()->cacheFile()).size() / 1024 / 1024 << "MB";
        s.roots = {root};
        s.hidden = false;
        FileIndex::instance()->setSettings(s);
        QVERIFY(FileIndex::instance()->waitIdle(10000));
    }

    void cacheAndRebuild()
    {
        QVERIFY(QFile::exists(FileIndex::instance()->cacheFile()));
        FileIndex::instance()->rebuild();
        QVERIFY(FileIndex::instance()->isBuilding());
        QVERIFY(FileIndex::instance()->waitIdle(10000));
        // A rebuild reads the disk again: the listed-only file is gone, the real ones are there.
        QVERIFY(names(QStringLiteral("cherry")).isEmpty());
        QVERIFY(names(QStringLiteral("zak")).contains(QStringLiteral("Zak McKracken.d64")));
        QVERIFY(FileIndex::instance()->count() > 10);
        // Off: nothing kept.
        FileIndex::Settings s = FileIndex::instance()->settings();
        s.enabled = false;
        FileIndex::instance()->setSettings(s);
        QVERIFY(!FileIndex::instance()->isReady());
        QVERIFY(!QFile::exists(FileIndex::instance()->cacheFile()));
    }
};

QTEST_GUILESS_MAIN(TestFileIndex)
#include "tst_fileindex.moc"
