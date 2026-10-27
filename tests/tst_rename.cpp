// Advanced Rename: naming rules, conflicts, safe renaming, undo, presets.
#include "core/RenameEngine.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {
void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(path.toUtf8());
}

QString readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}
} // namespace

class TestRename : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void standardWildcards()
    {
        RenameOptions o;
        o.oldPattern = QStringLiteral("IMG*.JPG");
        o.newPattern = QStringLiteral("Holiday *.jpg");
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral("IMG0001.JPG"), o, 1), QStringLiteral("Holiday 0001.jpg"));
        bool matched = true;
        RenameEngine::newNameFor(QStringLiteral("notes.txt"), o, 1, {}, {}, &matched);
        QVERIFY(!matched);
        // Opus-style bare # counters, padded to their length.
        o.oldPattern = QStringLiteral("*.*");
        o.newPattern = QStringLiteral("Track ## - *.*");
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral("song.mp3"), o, 7), QStringLiteral("Track 07 - song.mp3"));
        o.newPattern = QStringLiteral("No \\# [###].*");
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral("a.d64"), o, 3), QStringLiteral("No # 003.d64"));
        // Empty old pattern means everything.
        o.oldPattern.clear();
        o.newPattern = QStringLiteral("[parent] [#]");
        o.ignoreExtension = true;
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral("x.adf"), o, 2, {}, QStringLiteral("amiga")), QStringLiteral("amiga 2.adf"));
    }

    void findReplaceAndRegex()
    {
        RenameOptions o;
        o.mode = RenameOptions::Mode::FindReplace;
        o.oldPattern = QStringLiteral("_");
        o.newPattern = QStringLiteral(" ");
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral("deluxe_galaga_v2.lha"), o, 1), QStringLiteral("deluxe galaga v2.lha"));
        o.oldPattern = QStringLiteral("GALAGA");
        o.newPattern = QStringLiteral("Galaga");
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral("deluxe galaga.lha"), o, 1), QStringLiteral("deluxe Galaga.lha"));
        o.caseSensitive = true;
        bool matched = true;
        RenameEngine::newNameFor(QStringLiteral("deluxe galaga.lha"), o, 1, {}, {}, &matched);
        QVERIFY(!matched);

        RenameOptions r;
        r.mode = RenameOptions::Mode::RegExp;
        r.oldPattern = QStringLiteral("^(\\w+) - (\\w+)$");
        r.newPattern = QStringLiteral("\\2 - \\1");
        r.ignoreExtension = true;
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral("Artist - Title.mp3"), r, 1), QStringLiteral("Title - Artist.mp3"));
        r.newPattern = QStringLiteral("$2 ($1)");
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral("Artist - Title.mp3"), r, 1), QStringLiteral("Title (Artist).mp3"));
        r.oldPattern = QStringLiteral("([");
        RenameEngine::newNameFor(QStringLiteral("x"), r, 1, {}, {}, &matched);
        QVERIFY(!matched); // an invalid expression matches nothing
    }

    void caseAndTokens()
    {
        RenameOptions o;
        o.nameCase = RenameOptions::Case::Title;
        o.extCase = RenameOptions::ExtCase::Lower;
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral("THE last NINJA.D64"), o, 1), QStringLiteral("The Last Ninja.d64"));
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral("turrican ii (disk 2).adf"), o, 1), QStringLiteral("Turrican II (Disk 2).adf"));
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral("final fantasy vii - vivid.iso"), o, 1), QStringLiteral("Final Fantasy VII - Vivid.iso"));
        o.nameCase = RenameOptions::Case::Sentence;
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral("THE last NINJA.D64"), o, 1), QStringLiteral("The last ninja.d64"));
        o.nameCase = RenameOptions::Case::Upper;
        o.extCase = RenameOptions::ExtCase::Keep;
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral(".bashrc"), o, 1), QStringLiteral(".BASHRC")); // no extension
        RenameOptions t;
        t.newPattern = QStringLiteral("[mdate] [name].[ext]");
        const QDateTime when(QDate(1994, 6, 1), QTime(10, 45, 12));
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral("dispatch.txt"), t, 1, when), QStringLiteral("1994-06-01 dispatch.txt"));
        t.newPattern = QStringLiteral("[mtime] [nope].*");
        t.oldPattern = QStringLiteral("*.*");
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral("a.txt"), t, 1, when), QStringLiteral("104512 [nope].txt"));
        t.trimSpaces = true;
        t.newPattern = QStringLiteral("  a   b  .*");
        QCOMPARE(RenameEngine::newNameFor(QStringLiteral("x.txt"), t, 1), QStringLiteral("a b.txt"));
    }

    void previewCountsAndConflicts()
    {
        QTemporaryDir dir;
        const QString d = dir.path();
        for (const char *n : {"b.txt", "a.txt", "keep.md", "taken 1.txt"})
            touch(d + QLatin1Char('/') + QLatin1String(n));
        RenameOptions o;
        o.oldPattern = QStringLiteral("*.txt");
        o.newPattern = QStringLiteral("taken #.txt");
        o.counterStart = 1;
        // Counters follow the order given and skip items that do not match.
        QList<RenameItem> items = RenameEngine::preview({d + QStringLiteral("/a.txt"), d + QStringLiteral("/keep.md"), d + QStringLiteral("/b.txt")}, o);
        QCOMPARE(items.at(0).newName, QStringLiteral("taken 1.txt"));
        QCOMPARE(items.at(1).status, RenameItem::Status::NoMatch);
        QCOMPARE(items.at(2).newName, QStringLiteral("taken 2.txt"));
        QCOMPARE(items.at(0).status, RenameItem::Status::Conflict); // "taken 1.txt" exists
        QCOMPARE(items.at(2).status, RenameItem::Status::Ok);
        QVERIFY(RenameEngine::hasConflicts(items));
        QString err;
        QVERIFY(!RenameEngine::apply(items, nullptr, &err));
        QVERIFY(QFile::exists(d + QStringLiteral("/a.txt"))); // nothing renamed

        // Two items onto one name.
        o.newPattern = QStringLiteral("same.txt");
        items = RenameEngine::preview({d + QStringLiteral("/a.txt"), d + QStringLiteral("/b.txt")}, o);
        QCOMPARE(items.at(0).status, RenameItem::Status::Conflict);
        QCOMPARE(items.at(1).status, RenameItem::Status::Conflict);
        // Not allowed names.
        o.newPattern = QStringLiteral("a/b");
        items = RenameEngine::preview({d + QStringLiteral("/a.txt")}, o);
        QCOMPARE(items.at(0).status, RenameItem::Status::Invalid);
        // Unchanged.
        o.newPattern = QStringLiteral("*.txt");
        items = RenameEngine::preview({d + QStringLiteral("/a.txt")}, o);
        QCOMPARE(items.at(0).status, RenameItem::Status::Unchanged);
        QCOMPARE(RenameEngine::changeCount(items), 0);
    }

    void swapsCaseChangesAndUndo()
    {
        QTemporaryDir dir;
        const QString d = dir.path();
        touch(d + QStringLiteral("/one.txt"));
        touch(d + QStringLiteral("/two.txt"));
        touch(d + QStringLiteral("/Game.D64"));
        // one <-> two through Find and Replace in one batch.
        RenameOptions o;
        o.mode = RenameOptions::Mode::RegExp;
        o.oldPattern = QStringLiteral("^(one|two)$");
        o.ignoreExtension = true;
        o.newPattern = QStringLiteral("x");
        // Build the swap by hand: one->two, two->one.
        QList<RenameItem> items;
        RenameItem a;
        a.path = d + QStringLiteral("/one.txt");
        a.oldName = QStringLiteral("one.txt");
        a.newName = QStringLiteral("two.txt");
        a.status = RenameItem::Status::Ok;
        RenameItem b = a;
        b.path = d + QStringLiteral("/two.txt");
        b.oldName = QStringLiteral("two.txt");
        b.newName = QStringLiteral("one.txt");
        items << a << b;
        const QString oneText = readAll(d + QStringLiteral("/one.txt"));
        QList<QPair<QString, QString>> done;
        QString err;
        QVERIFY2(RenameEngine::apply(items, &done, &err), qPrintable(err));
        QCOMPARE(readAll(d + QStringLiteral("/two.txt")), oneText);
        // The preview sees swaps as fine too: each target is being renamed away.
        RenameOptions sw;
        sw.mode = RenameOptions::Mode::FindReplace;
        sw.oldPattern = QStringLiteral("Game");
        sw.newPattern = QStringLiteral("game");
        sw.caseSensitive = true;
        sw.extCase = RenameOptions::ExtCase::Lower;
        QList<RenameItem> c = RenameEngine::preview({d + QStringLiteral("/Game.D64")}, sw);
        QCOMPARE(c.at(0).newName, QStringLiteral("game.d64"));
        QCOMPARE(c.at(0).status, RenameItem::Status::Ok);
        QVERIFY(RenameEngine::apply(c, nullptr, &err));
        QVERIFY(QFile::exists(d + QStringLiteral("/game.d64")));
        // Undo the swap.
        QVERIFY2(RenameEngine::undo(done, &err), qPrintable(err));
        QCOMPARE(readAll(d + QStringLiteral("/one.txt")), oneText);
        // No temporary files left behind.
        QCOMPARE(QDir(d).entryList(QDir::Files | QDir::Hidden).filter(QStringLiteral(".dirduo-rename")).size(), 0);
    }

    void presets()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("rename.ini"));
        RenameOptions o;
        o.mode = RenameOptions::Mode::RegExp;
        o.oldPattern = QStringLiteral("(.*)");
        o.newPattern = QStringLiteral("\\1 [##]");
        o.nameCase = RenameOptions::Case::Title;
        o.extCase = RenameOptions::ExtCase::Upper;
        o.counterStart = 5;
        o.counterStep = 5;
        o.ignoreExtension = true;
        QVERIFY(RenamePresets::save(QStringLiteral("Number them / tidy"), o, file));
        QCOMPARE(RenamePresets::names(file), QStringList{QStringLiteral("Number them / tidy")});
        RenameOptions back;
        QVERIFY(RenamePresets::load(QStringLiteral("Number them / tidy"), back, file));
        QCOMPARE(back.mode, o.mode);
        QCOMPARE(back.newPattern, o.newPattern);
        QCOMPARE(back.nameCase, o.nameCase);
        QCOMPARE(back.extCase, o.extCase);
        QCOMPARE(back.counterStart, 5);
        QVERIFY(back.ignoreExtension);
        QVERIFY(RenamePresets::remove(QStringLiteral("Number them / tidy"), file));
        QVERIFY(RenamePresets::names(file).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRename)
#include "tst_rename.moc"
