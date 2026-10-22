// Undo of copies, moves, the trash and renames, and the file log.
#include "core/History.h"
#include "core/JobManager.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        qFatal("cannot write %s", qPrintable(path));
    f.write(data);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestHistory : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<JobManager> m_jobs;
    std::unique_ptr<UndoHistory> m_history;
    QString src, dst;

    void run(JobType type, const QStringList &sources, const QString &dest = {},
             CollisionAction when = CollisionAction::Replace)
    {
        JobSpec s;
        s.type = type;
        s.sources = sources;
        s.destDir = dest;
        s.whenExists = when;
        m_jobs->enqueue(s);
        QVERIFY(m_jobs->waitForIdle(10000));
        QCoreApplication::processEvents();
    }

private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        src = m_dir->filePath(QStringLiteral("src"));
        dst = m_dir->filePath(QStringLiteral("dst"));
        QDir().mkpath(src);
        QDir().mkpath(dst);
        Paths::setDataHomeOverride(m_dir->filePath(QStringLiteral("data")));
        m_jobs = std::make_unique<JobManager>();
        m_history = std::make_unique<UndoHistory>(*m_jobs);
    }

    void cleanup()
    {
        m_history.reset();
        m_jobs.reset();
    }

    void undoCopyKeepsWhatWasThere()
    {
        writeFile(src + QStringLiteral("/a.txt"), "new a");
        writeFile(src + QStringLiteral("/b.txt"), "new b");
        writeFile(dst + QStringLiteral("/b.txt"), "old b"); // will be replaced
        // Merging into a folder that is already there.
        writeFile(src + QStringLiteral("/d/new.txt"), "new");
        writeFile(dst + QStringLiteral("/d/old.txt"), "old");
        run(JobType::Copy, {src + QStringLiteral("/a.txt"), src + QStringLiteral("/b.txt"), src + QStringLiteral("/d")}, dst);
        QCOMPARE(m_history->entries().size(), 1);
        const UndoEntry e = m_history->entries().first();
        QCOMPARE(e.kind, UndoEntry::Kind::Copy);
        QCOMPARE(e.items.size(), 3); // a.txt, b.txt and d/new.txt; not d itself
        QCOMPARE(e.replaced, 1);
        QVERIFY(e.title.startsWith(QStringLiteral("Copy 3 items to dst")));

        QStringList touched;
        QString err;
        QVERIFY(m_history->undo(e.id, &touched, &err));
        QVERIFY(err.contains(QStringLiteral("replaced"))); // told why b.txt stays
        QVERIFY(!QFileInfo::exists(dst + QStringLiteral("/a.txt")));
        QCOMPARE(readFile(dst + QStringLiteral("/b.txt")), QByteArray("new b"));
        QVERIFY(!QFileInfo::exists(dst + QStringLiteral("/d/new.txt")));
        QVERIFY(QFileInfo::exists(dst + QStringLiteral("/d/old.txt"))); // never touched
        QVERIFY(QFileInfo::exists(src + QStringLiteral("/a.txt")));
        // The copies went to the trash, and that can be undone (redo).
        QCOMPARE(m_history->entries().first().kind, UndoEntry::Kind::Trash);
        QVERIFY(m_history->entries().first().isUndo);
        QVERIFY(m_history->undo(m_history->entries().first().id, &touched, &err));
        QCOMPARE(readFile(dst + QStringLiteral("/a.txt")), QByteArray("new a"));
        QVERIFY(touched.contains(dst + QStringLiteral("/a.txt")));
    }

    void undoMove()
    {
        writeFile(src + QStringLiteral("/album/01.mp3"), "one");
        writeFile(src + QStringLiteral("/single.txt"), "s");
        run(JobType::Move, {src + QStringLiteral("/album"), src + QStringLiteral("/single.txt")}, dst);
        QVERIFY(QFileInfo::exists(dst + QStringLiteral("/album/01.mp3")));
        QVERIFY(!QFileInfo::exists(src + QStringLiteral("/album")));
        const UndoEntry e = m_history->entries().first();
        QCOMPARE(e.kind, UndoEntry::Kind::Move);
        QStringList touched;
        QString err;
        QVERIFY2(m_history->undo(e.id, &touched, &err), qPrintable(err));
        QCOMPARE(readFile(src + QStringLiteral("/album/01.mp3")), QByteArray("one"));
        QVERIFY(QFileInfo::exists(src + QStringLiteral("/single.txt")));
        QVERIFY(!QFileInfo::exists(dst + QStringLiteral("/album")));
        // And redo.
        QVERIFY(m_history->undo(m_history->entries().first().id, &touched, &err));
        QVERIFY(QFileInfo::exists(dst + QStringLiteral("/album/01.mp3")));
    }

    void undoMoveWhenTheFolderWasRemoved()
    {
        // The folder it came from is gone: it is made again.
        writeFile(src + QStringLiteral("/deep/inside/x.txt"), "x");
        run(JobType::Move, {src + QStringLiteral("/deep/inside/x.txt")}, dst);
        QVERIFY(QDir(src + QStringLiteral("/deep")).removeRecursively());
        QStringList touched;
        QString err;
        QVERIFY(m_history->undo(m_history->entries().first().id, &touched, &err));
        QCOMPARE(readFile(src + QStringLiteral("/deep/inside/x.txt")), QByteArray("x"));
    }

    void undoTrash()
    {
        writeFile(src + QStringLiteral("/gone.txt"), "bye");
        writeFile(src + QStringLiteral("/folder/in.txt"), "in");
        run(JobType::Trash, {src + QStringLiteral("/gone.txt"), src + QStringLiteral("/folder")});
        QVERIFY(!QFileInfo::exists(src + QStringLiteral("/gone.txt")));
        const UndoEntry e = m_history->entries().first();
        QCOMPARE(e.kind, UndoEntry::Kind::Trash);
        const QString trashed = e.items.first().second;
        QVERIFY(QFileInfo::exists(trashed));
        const QString info = QFileInfo(QFileInfo(trashed).absolutePath()).absolutePath() + QStringLiteral("/info/")
            + QFileInfo(trashed).fileName() + QStringLiteral(".trashinfo");
        QVERIFY(QFileInfo::exists(info));
        QStringList touched;
        QString err;
        QVERIFY(m_history->undo(e.id, &touched, &err));
        QCOMPARE(readFile(src + QStringLiteral("/gone.txt")), QByteArray("bye"));
        QCOMPARE(readFile(src + QStringLiteral("/folder/in.txt")), QByteArray("in"));
        QVERIFY(!QFileInfo::exists(info)); // no stale entry left in the trash
        QCOMPARE(m_history->entries().first().kind, UndoEntry::Kind::Restore);

        // Something new in the way: not overwritten.
        run(JobType::Trash, {src + QStringLiteral("/gone.txt")});
        writeFile(src + QStringLiteral("/gone.txt"), "newer");
        QVERIFY(!m_history->undo(m_history->entries().first().id, &touched, &err));
        QVERIFY(err.contains(QStringLiteral("already called")));
        QCOMPARE(readFile(src + QStringLiteral("/gone.txt")), QByteArray("newer"));
    }

    void renamesAndNewItems()
    {
        writeFile(src + QStringLiteral("/new name.txt"), "r");
        UndoEntry r;
        r.kind = UndoEntry::Kind::Rename;
        r.items = {qMakePair(src + QStringLiteral("/new name.txt"), src + QStringLiteral("/old name.txt"))};
        const int id = m_history->add(r);
        QStringList touched;
        QString err;
        QVERIFY(m_history->undo(id, &touched, &err));
        QVERIFY(QFileInfo::exists(src + QStringLiteral("/old name.txt")));
        QCOMPARE(touched, QStringList{src + QStringLiteral("/old name.txt")});
        QVERIFY(m_history->latestRename() == nullptr); // only an undo of one is left
        QVERIFY(m_history->undo(m_history->entries().first().id, &touched, &err)); // redo
        QVERIFY(QFileInfo::exists(src + QStringLiteral("/new name.txt")));

        QDir().mkpath(src + QStringLiteral("/Made"));
        UndoEntry n;
        n.kind = UndoEntry::Kind::NewItem;
        n.items = {qMakePair(src + QStringLiteral("/Made"), src + QStringLiteral("/Made"))};
        QVERIFY(m_history->undo(m_history->add(n), &touched, &err));
        QVERIFY(!QFileInfo::exists(src + QStringLiteral("/Made")));
        QVERIFY(!m_history->undo(9999, &touched, &err));
    }

    void deleteIsLoggedNotUndoable()
    {
        writeFile(src + QStringLiteral("/forever.txt"), "x");
        run(JobType::Delete, {src + QStringLiteral("/forever.txt")});
        QVERIFY(m_history->entries().isEmpty());
        const QList<FileLog::Entry> log = FileLog::read();
        QVERIFY(!log.isEmpty());
        QCOMPARE(log.last().action, QStringLiteral("Delete"));
        QCOMPARE(log.last().item, src + QStringLiteral("/forever.txt"));
    }

    void fileLog()
    {
        writeFile(src + QStringLiteral("/Galaga.d64"), "g");
        run(JobType::Copy, {src + QStringLiteral("/Galaga.d64")}, dst);
        QList<FileLog::Entry> log = FileLog::read();
        QCOMPARE(log.size(), 1);
        QCOMPARE(log.first().action, QStringLiteral("Copy"));
        QCOMPARE(log.first().to, dst + QStringLiteral("/Galaga.d64"));
        QStringList touched;
        QString err;
        QVERIFY(m_history->undo(m_history->entries().first().id, &touched, &err));
        log = FileLog::read();
        QCOMPARE(log.last().action, QStringLiteral("Undo: Trash"));
        QVERIFY(FileLog::toText(log).contains(QStringLiteral("Galaga.d64  ->  ")));
        // Tabs and line breaks in names cannot break the file apart.
        FileLog::append(FileLog::Entry{QDateTime::currentDateTime(), QStringLiteral("Copy"), QStringLiteral("odd\tname\nhere"), {}, {}});
        QCOMPARE(FileLog::read().last().item, QStringLiteral("odd name here"));
        FileLog::clear();
        QVERIFY(FileLog::read().isEmpty());
    }

    void keepsAHundred()
    {
        for (int i = 0; i < UndoHistory::kKeep + 20; ++i) {
            UndoEntry e;
            e.kind = UndoEntry::Kind::NewItem;
            e.items = {qMakePair(QStringLiteral("/nope/%1").arg(i), QStringLiteral("/nope/%1").arg(i))};
            m_history->add(e);
        }
        QCOMPARE(m_history->entries().size(), UndoHistory::kKeep);
    }
};

QTEST_GUILESS_MAIN(TestHistory)
#include "tst_history.moc"
