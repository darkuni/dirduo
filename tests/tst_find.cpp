// Find (criteria, contents, presets) and collections (store, renames, lister).
#include "core/Collections.h"
#include "core/DirLister.h"
#include "core/FindEngine.h"
#include "core/Location.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
void write(const QString &path, const QByteArray &data, int ageDays = 0)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
    f.close();
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(QDateTime::currentDateTime().addDays(-ageDays), QFileDevice::FileModificationTime));
}

QStringList names(const QStringList &paths)
{
    QStringList out;
    for (const QString &p : paths)
        out << Location::fileName(p);
    out.sort();
    return out;
}
} // namespace

class TestFind : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        Paths::setConfigDirOverride(m_dir->filePath(QStringLiteral("config")));
        root = m_dir->filePath(QStringLiteral("roms"));
        write(root + QStringLiteral("/c64/Galaga.d64"), QByteArray(170000, 'g'), 400);
        write(root + QStringLiteral("/c64/Elite.d64"), QByteArray(170000, 'e'), 3);
        write(root + QStringLiteral("/amiga/Deluxe Galaga.adf"), QByteArray(900000, 'd'), 10);
        write(root + QStringLiteral("/docs/readme.txt"), "Insert DISK 2 and press fire", 1);
        write(root + QStringLiteral("/docs/notes.txt"), "nothing here", 1);
        write(root + QStringLiteral("/.cache/galaga.tmp"), "x", 1);
    }

    void byName()
    {
        FindCriteria c;
        c.name = QStringLiteral("galaga");
        QCOMPARE(names(FindEngine::find({root}, c)), (QStringList{QStringLiteral("Deluxe Galaga.adf"), QStringLiteral("Galaga.d64")}));
        c.includeHidden = true;
        QCOMPARE(FindEngine::find({root}, c).size(), 3);
        c.includeHidden = false;
        c.name = QStringLiteral("*.d64;*.adf");
        QCOMPARE(FindEngine::find({root}, c).size(), 3);
        c.name = QStringLiteral("^G.*\\.d64$");
        c.nameRegex = true;
        c.caseSensitive = true;
        QCOMPARE(names(FindEngine::find({root}, c)), QStringList{QStringLiteral("Galaga.d64")});
        c.name = QStringLiteral("([");
        QVERIFY(FindEngine::find({root}, c).isEmpty()); // broken expression finds nothing
        // Folders.
        FindCriteria f;
        f.name = QStringLiteral("c64");
        f.type = FindCriteria::Type::Folders;
        QCOMPARE(FindEngine::find({root}, f), QStringList{root + QStringLiteral("/c64")});
        f.recurse = false;
        f.name = QStringLiteral("*.d64");
        f.type = FindCriteria::Type::Files;
        QVERIFY(FindEngine::find({root}, f).isEmpty());
    }

    void bySizeDateAndContents()
    {
        FindCriteria c;
        c.minSize = FindCriteria::parseSize(QStringLiteral("500K"));
        QCOMPARE(names(FindEngine::find({root}, c)), QStringList{QStringLiteral("Deluxe Galaga.adf")});
        c.minSize = -1;
        c.maxSize = FindCriteria::parseSize(QStringLiteral("1k"));
        QCOMPARE(FindEngine::find({root}, c).size(), 2);
        QCOMPARE(FindCriteria::parseSize(QStringLiteral("2G")), 2LL * 1024 * 1024 * 1024);
        QCOMPARE(FindCriteria::parseSize(QStringLiteral("1.5MB")), qint64(1.5 * 1024 * 1024));
        QCOMPARE(FindCriteria::parseSize(QStringLiteral("big")), qint64(-1));

        FindCriteria d;
        d.newerThan = FindCriteria::parseWhen(QStringLiteral("7"));
        d.name = QStringLiteral("*.d64;*.adf");
        QCOMPARE(names(FindEngine::find({root}, d)), QStringList{QStringLiteral("Elite.d64")});
        d.newerThan = {};
        d.olderThan = FindCriteria::parseWhen(QStringLiteral("365"));
        QCOMPARE(names(FindEngine::find({root}, d)), QStringList{QStringLiteral("Galaga.d64")});
        QVERIFY(FindCriteria::parseWhen(QStringLiteral("2024-01-31")).isValid());
        QVERIFY(!FindCriteria::parseWhen(QStringLiteral("someday")).isValid());

        FindCriteria t;
        t.contains = QStringLiteral("disk 2");
        QCOMPARE(names(FindEngine::find({root}, t)), QStringList{QStringLiteral("readme.txt")});
        t.caseSensitive = true;
        QVERIFY(FindEngine::find({root}, t).isEmpty());
        // A match across the 1 MB read boundary is still found.
        QByteArray big(1024 * 1024 - 3, 'x');
        big += "NEEDLE";
        write(root + QStringLiteral("/docs/big.bin"), big);
        QVERIFY(FindEngine::fileContains(root + QStringLiteral("/docs/big.bin"), QStringLiteral("needle"), false));

        bool truncated = false;
        FindCriteria all;
        QCOMPARE(FindEngine::find({root}, all, nullptr, 2, &truncated).size(), 2);
        QVERIFY(truncated);
    }

    void savedSearches()
    {
        FindCriteria c;
        c.name = QStringLiteral("*.d64");
        c.newerThan = FindCriteria::parseWhen(QStringLiteral("30"));
        c.minSize = 1000;
        QVERIFY(FindPresets::save(QStringLiteral("Recent C64"), c, {root}));
        QCOMPARE(FindPresets::names(), QStringList{QStringLiteral("Recent C64")});
        FindCriteria back;
        QStringList roots;
        QVERIFY(FindPresets::load(QStringLiteral("Recent C64"), back, &roots));
        QCOMPARE(back.name, c.name);
        QCOMPARE(back.minSize, qint64(1000));
        QCOMPARE(back.newerThan.date(), QDate::currentDate().addDays(-30)); // stays relative
        QCOMPARE(roots, QStringList{root});
        QVERIFY(FindPresets::remove(QStringLiteral("Recent C64")));
    }

    void collectionsStore()
    {
        Collections *c = Collections::instance();
        QSignalSpy changed(c, &Collections::changed);
        QVERIFY(c->create(QStringLiteral("C64 favorites")));
        QVERIFY(!c->create(QStringLiteral("C64 favorites")));
        QVERIFY(!c->create(QStringLiteral("a/b")));
        QCOMPARE(c->add(QStringLiteral("C64 favorites"), {root + QStringLiteral("/c64/Galaga.d64"), root + QStringLiteral("/c64")}), 2);
        QCOMPARE(c->add(QStringLiteral("C64 favorites"), {root + QStringLiteral("/c64/Galaga.d64")}), 0); // once only
        QVERIFY(changed.count() >= 2);
        // Renames follow, including things inside a renamed folder.
        c->renamed(root + QStringLiteral("/c64"), root + QStringLiteral("/commodore"));
        QCOMPARE(c->items(QStringLiteral("C64 favorites")),
                 (QStringList{root + QStringLiteral("/commodore/Galaga.d64"), root + QStringLiteral("/commodore")}));
        QCOMPARE(c->removeItems(QStringLiteral("C64 favorites"), {root + QStringLiteral("/commodore")}), 1);
        QVERIFY(c->rename(QStringLiteral("C64 favorites"), QStringLiteral("Keepers")));
        QCOMPARE(c->names(), QStringList{QStringLiteral("Keepers")});
        // Locations.
        const QString loc = Collections::location(QStringLiteral("Find Results"));
        QVERIFY(Collections::isCollectionLocation(loc));
        QCOMPARE(Collections::nameOf(loc), QStringLiteral("Find Results"));
        QCOMPARE(Location::clean(loc), loc);
        QCOMPARE(Location::title(loc), QStringLiteral("Find Results"));
        QCOMPARE(Location::parent(loc), QStringLiteral("coll:/"));
        QVERIFY(Location::isRoot(QStringLiteral("coll:/")));
        QCOMPARE(Location::title(QStringLiteral("coll:/")), QStringLiteral("Collections"));
        QVERIFY(c->remove(QStringLiteral("Keepers")));
        QVERIFY(c->names().isEmpty());
    }

    void listerShowsCollections()
    {
        Collections *c = Collections::instance();
        c->setItems(QStringLiteral("Picks"), {root + QStringLiteral("/c64/Elite.d64"), root + QStringLiteral("/amiga/Deluxe Galaga.adf"),
                                             root + QStringLiteral("/gone.d64")});
        DirLister lister;
        QSignalSpy listed(&lister, &DirLister::listed);
        lister.open(QStringLiteral("coll:/"));
        QTRY_COMPARE(listed.count(), 1);
        FileItemList items = listed.takeFirst().at(1).value<FileItemList>();
        QCOMPARE(items.size(), 1);
        QVERIFY(items.first().isDir);
        QCOMPARE(items.first().path, Collections::location(QStringLiteral("Picks")));

        lister.open(Collections::location(QStringLiteral("Picks")));
        QTRY_COMPARE(listed.count(), 1);
        items = listed.takeFirst().at(1).value<FileItemList>();
        QCOMPARE(items.size(), 2); // the missing one is left out
        QCOMPARE(items.first().path, root + QStringLiteral("/c64/Elite.d64")); // the real file
        QCOMPARE(items.first().relPath, root + QStringLiteral("/c64"));
        // Changes show up by themselves.
        c->add(QStringLiteral("Picks"), {root + QStringLiteral("/docs/readme.txt")});
        QTRY_COMPARE(listed.count(), 1);
        QCOMPARE(listed.takeFirst().at(1).value<FileItemList>().size(), 3);
        c->remove(QStringLiteral("Picks"));
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QString root;
};

QTEST_GUILESS_MAIN(TestFind)
#include "tst_find.moc"
