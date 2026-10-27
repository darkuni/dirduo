#include "core/AliasManager.h"
#include "core/Archive.h"
#include "core/CommandEngine.h"
#include "core/History.h"
#include "core/JobManager.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#ifdef DIRDUO_HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

class Ctx : public CommandContext
{
public:
    QString source, dest;
    QStringList selection;
    int tools = 0;
    QString sourcePath() const override { return source; }
    QString destPath() const override { return dest; }
    QStringList selectedPaths() const override { return selection; }
    QString focusedPath() const override { return selection.value(0); }
    bool promptText(const QString &, const QString &, QString &) override { return false; }
    QStringList passwords;
    int asked = 0;
    bool promptPassword(const QString &, const QString &, QString &value) override
    {
        ++asked;
        if (passwords.isEmpty())
            return false;
        value = passwords.takeFirst();
        return true;
    }
    bool confirm(const QString &, const QString &) override { return true; }
    QString lastError;
    void showError(const QString &, const QString &text) override { lastError = text; }
    QString chooseDestination(const QString &) override { return {}; }
    bool go(const ParsedCommand &, QString *) override { return true; }
    bool set(const ParsedCommand &, QString *) override { return true; }
    bool select(const ParsedCommand &, QString *) override { return true; }
    bool beginInlineRename() override { return true; }
    void afterLocalChange(const QStringList &) override {}
    bool tool(const ParsedCommand &, QString *) override
    {
        ++tools;
        return true;
    }
};

} // namespace

class TestArchive : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        if (!Archive::available())
            QSKIP("Built without libarchive");
        m_dir = std::make_unique<QTemporaryDir>();
        src = m_dir->filePath(QStringLiteral("src"));
        dst = m_dir->filePath(QStringLiteral("dst"));
        QDir().mkpath(src);
        QDir().mkpath(dst);
        Paths::setDataHomeOverride(m_dir->filePath(QStringLiteral("data")));
        writeFile(src + QStringLiteral("/Game/disk1.d64"), QByteArray(200000, 'd'));
        writeFile(src + QStringLiteral("/Game/docs/readme.txt"), "Ünïcode readme");
        QVERIFY(QFile::link(QStringLiteral("disk1.d64"), src + QStringLiteral("/Game/current")));
        writeFile(src + QStringLiteral("/notes.txt"), "notes");
    }

    void names()
    {
        Archive::Format f;
        QVERIFY(Archive::formatFromName(QStringLiteral("x.tar.gz"), f));
        QCOMPARE(f, Archive::Format::TarGz);
        QVERIFY(Archive::formatFromName(QStringLiteral("X.ZIP"), f));
        QCOMPARE(f, Archive::Format::Zip);
        QVERIFY(Archive::formatFromKey(QStringLiteral("7z"), f));
        QCOMPARE(Archive::extensionFor(f), QStringLiteral("7z"));
        QVERIFY(!Archive::formatFromName(QStringLiteral("x.txt"), f));
        QVERIFY(Archive::isArchive(QStringLiteral("/a/Games.rar")));
        QVERIFY(Archive::isArchive(QStringLiteral("/a/b.tar.zst")));
        QVERIFY(!Archive::isArchive(QStringLiteral("/a/b.d64")));
        QCOMPARE(Archive::baseName(QStringLiteral("/a/Games.tar.gz")), QStringLiteral("Games"));
        QCOMPARE(Archive::baseName(QStringLiteral("/a/Games.zip")), QStringLiteral("Games"));
    }

    void roundTrip_data()
    {
        QTest::addColumn<QString>("key");
        for (Archive::Format f : Archive::formats())
            QTest::newRow(qPrintable(Archive::extensionFor(f))) << Archive::extensionFor(f);
    }

    void roundTrip()
    {
        QFETCH(QString, key);
        Archive::Format f;
        QVERIFY(Archive::formatFromKey(key, f));
        const QString path = m_dir->filePath(QStringLiteral("out.") + key);
        QString error;
        int items = 0;
        Archive::Hooks hooks;
        hooks.current = [&](const QString &, qint64) { ++items; };
        if (!Archive::pack({src + QStringLiteral("/Game"), src + QStringLiteral("/notes.txt")}, path, f, hooks, &error)) {
            if (error.contains(QStringLiteral("not supported"), Qt::CaseInsensitive) || error.contains(QStringLiteral("zstd")))
                QSKIP(qPrintable(QStringLiteral("This libarchive cannot write %1: %2").arg(key, error)));
            QFAIL(qPrintable(error));
        }
        QVERIFY(items >= 4);
        QCOMPARE(Archive::topLevelNames(path, &error), (QStringList{QStringLiteral("Game"), QStringLiteral("notes.txt")}));

        QStringList created;
        QVERIFY2(Archive::unpack(path, dst, {}, false, {}, &created, &error), qPrintable(error));
        QCOMPARE(readFile(dst + QStringLiteral("/Game/disk1.d64")).size(), 200000);
        QCOMPARE(readFile(dst + QStringLiteral("/Game/docs/readme.txt")), QByteArray("Ünïcode readme"));
        QCOMPARE(readFile(dst + QStringLiteral("/notes.txt")), QByteArray("notes"));
        QVERIFY(QFileInfo(dst + QStringLiteral("/Game/current")).isSymLink());
        QCOMPARE(created.size(), 2);
    }

    void keepsOrReplacesExisting()
    {
        const QString path = m_dir->filePath(QStringLiteral("n.zip"));
        QString error;
        QVERIFY(Archive::pack({src + QStringLiteral("/notes.txt")}, path, Archive::Format::Zip, {}, &error));
        writeFile(dst + QStringLiteral("/notes.txt"), "mine");
        QStringList problems;
        Archive::Hooks hooks;
        hooks.problem = [&](const QString &m) { problems << m; };
        Archive::unpack(path, dst, {}, false, hooks, nullptr, &error);
        QCOMPARE(readFile(dst + QStringLiteral("/notes.txt")), QByteArray("mine"));
        QVERIFY(Archive::unpack(path, dst, {}, true, {}, nullptr, &error));
        QCOMPARE(readFile(dst + QStringLiteral("/notes.txt")), QByteArray("notes"));
        // Renamed on the way in.
        QVERIFY(Archive::unpack(path, dst, {{QStringLiteral("notes.txt"), QStringLiteral("notes (2).txt")}}, false, {}, nullptr, &error));
        QCOMPARE(readFile(dst + QStringLiteral("/notes (2).txt")), QByteArray("notes"));
    }

#ifdef DIRDUO_HAVE_LIBARCHIVE
    void refusesToWriteOutside()
    {
        const QString path = m_dir->filePath(QStringLiteral("evil.tar"));
        archive *a = archive_write_new();
        archive_write_set_format_pax_restricted(a);
        QCOMPARE(archive_write_open_filename(a, QFile::encodeName(path).constData()), ARCHIVE_OK);
        for (const char *name : {"../escaped.txt", "/tmp/dirduo-absolute.txt", "fine.txt"}) {
            archive_entry *e = archive_entry_new();
            archive_entry_set_pathname(e, name);
            archive_entry_set_filetype(e, AE_IFREG);
            archive_entry_set_perm(e, 0644);
            archive_entry_set_size(e, 2);
            archive_write_header(a, e);
            archive_write_data(a, "hi", 2);
            archive_entry_free(e);
        }
        archive_write_close(a);
        archive_write_free(a);

        const QString into = dst + QStringLiteral("/in");
        QDir().mkpath(into);
        QString error;
        Archive::unpack(path, into, {}, true, {}, nullptr, &error);
        QVERIFY(!QFile::exists(dst + QStringLiteral("/escaped.txt")));
        QVERIFY(!QFile::exists(QStringLiteral("/tmp/dirduo-absolute.txt")));
        QVERIFY(QFile::exists(into + QStringLiteral("/fine.txt")));
    }
#endif

    void passwords()
    {
        const QString path = m_dir->filePath(QStringLiteral("locked.zip"));
        QString error;
        Archive::Hooks lock;
        lock.password = QStringLiteral("se\"cr{et}");
        if (!Archive::pack({src + QStringLiteral("/Game"), src + QStringLiteral("/notes.txt")}, path, Archive::Format::Zip, lock, &error)) {
            if (error.contains(QStringLiteral("cannot encrypt")))
                QSKIP("This libarchive was built without encryption");
            QFAIL(qPrintable(error));
        }
        QVERIFY(Archive::isEncrypted(path));
        QVERIFY(!Archive::isEncrypted(m_dir->filePath(QStringLiteral("nope.zip"))));
        // Other formats cannot take one.
        QVERIFY(!Archive::pack({src + QStringLiteral("/notes.txt")}, m_dir->filePath(QStringLiteral("x.7z")), Archive::Format::SevenZip, lock, &error));
        QVERIFY(error.contains(QStringLiteral("Only zip")));

        const QString none = dst + QStringLiteral("/none");
        QDir().mkpath(none);
        QVERIFY(!Archive::unpack(path, none, {}, false, {}, nullptr, &error));
        QCOMPARE(error, QStringLiteral("It is protected by a password."));
        Archive::Hooks wrong;
        wrong.password = QStringLiteral("nope");
        QVERIFY(!Archive::unpack(path, none, {}, true, wrong, nullptr, &error));
        QCOMPARE(error, QStringLiteral("The password is wrong."));
        QVERIFY2(Archive::unpack(path, dst, {}, true, lock, nullptr, &error), qPrintable(error));
        QCOMPARE(readFile(dst + QStringLiteral("/notes.txt")), QByteArray("notes"));
        QCOMPARE(readFile(dst + QStringLiteral("/Game/disk1.d64")).size(), 200000);

        // As a job: a wrong password leaves no empty folder behind.
        JobManager jm;
        JobSpec s;
        s.type = JobType::Unpack;
        s.destDir = dst;
        s.sources = {path};
        s.extractMode = JobSpec::ExtractMode::Folder;
        s.password = QStringLiteral("nope");
        int id = jm.enqueue(s);
        QVERIFY(jm.waitForIdle(10000));
        QVERIFY(!jm.snapshot(id).errors.isEmpty());
        QVERIFY(!QFile::exists(dst + QStringLiteral("/locked")));
        s.password = lock.password;
        id = jm.enqueue(s);
        QVERIFY(jm.waitForIdle(10000));
        QCOMPARE(jm.snapshot(id).errors, QStringList());
        QVERIFY(QFile::exists(dst + QStringLiteral("/locked/notes.txt")));
    }

    void packJob()
    {
        JobManager jm;
        JobSpec s;
        s.type = JobType::Pack;
        s.sources = {src + QStringLiteral("/Game"), src + QStringLiteral("/notes.txt")};
        s.destDir = dst;
        s.archiveName = QStringLiteral("Backup.zip");
        s.archiveFormat = QStringLiteral("zip");
        int id = jm.enqueue(s);
        QVERIFY(jm.waitForIdle(10000));
        QCOMPARE(jm.snapshot(id).state, JobState::Finished);
        QVERIFY(QFile::exists(dst + QStringLiteral("/Backup.zip")));
        QVERIFY(!QFile::exists(dst + QStringLiteral("/Backup.zip.dirduo-part")));
        QCOMPARE(jm.outcomes(id).value(0).target, dst + QStringLiteral("/Backup.zip"));
        // Taken: a new name, unless told to replace or skip.
        s.whenExists = CollisionAction::Rename;
        id = jm.enqueue(s);
        QVERIFY(jm.waitForIdle(10000));
        QVERIFY(QFile::exists(dst + QStringLiteral("/Backup (2).zip")));
        s.whenExists = CollisionAction::Skip;
        id = jm.enqueue(s);
        QVERIFY(jm.waitForIdle(10000));
        QVERIFY(!QFile::exists(dst + QStringLiteral("/Backup (3).zip")));
    }

    void unpackJobModes()
    {
        QString error;
        const QString one = m_dir->filePath(QStringLiteral("One.zip"));
        const QString many = m_dir->filePath(QStringLiteral("Many.tar.gz"));
        QVERIFY(Archive::pack({src + QStringLiteral("/Game")}, one, Archive::Format::Zip, {}, &error));
        QVERIFY(Archive::pack({src + QStringLiteral("/Game"), src + QStringLiteral("/notes.txt")}, many, Archive::Format::TarGz, {}, &error));

        JobManager jm;
        JobSpec s;
        s.type = JobType::Unpack;
        s.destDir = dst;
        // Smart: one folder inside goes straight in; several get their own folder.
        s.sources = {one, many};
        int id = jm.enqueue(s);
        QVERIFY(jm.waitForIdle(10000));
        QCOMPARE(jm.snapshot(id).errors, QStringList());
        QVERIFY(QFile::exists(dst + QStringLiteral("/Game/disk1.d64")));
        QVERIFY(QFile::exists(dst + QStringLiteral("/Many/Game/disk1.d64")));
        QVERIFY(QFile::exists(dst + QStringLiteral("/Many/notes.txt")));
        QCOMPARE(jm.outcomes(id).size(), 2);
        // Again: nothing is overwritten; the new copies get new names.
        id = jm.enqueue(s);
        QVERIFY(jm.waitForIdle(10000));
        QVERIFY(QFile::exists(dst + QStringLiteral("/Game (2)/disk1.d64")));
        QVERIFY(QFile::exists(dst + QStringLiteral("/Many (2)/notes.txt")));
        // Folder: always its own folder. Here: straight in.
        s.sources = {one};
        s.extractMode = JobSpec::ExtractMode::Folder;
        id = jm.enqueue(s);
        QVERIFY(jm.waitForIdle(10000));
        QVERIFY(QFile::exists(dst + QStringLiteral("/One/Game/disk1.d64")));
        const QString here = dst + QStringLiteral("/here");
        QDir().mkpath(here);
        s.sources = {many};
        s.destDir = here;
        s.extractMode = JobSpec::ExtractMode::Here;
        id = jm.enqueue(s);
        QVERIFY(jm.waitForIdle(10000));
        QVERIFY(QFile::exists(here + QStringLiteral("/notes.txt")));
        QVERIFY(QFile::exists(here + QStringLiteral("/Game/docs/readme.txt")));
    }

    void commands()
    {
        AliasManager aliases(m_dir->filePath(QStringLiteral("aliases.ini")));
        JobManager jm;
        CommandEngine engine(aliases, jm);
        Ctx ctx;
        ctx.source = src;
        ctx.dest = dst;
        QString error;
        auto run = [&](const QString &line) {
            ctx.lastError.clear();
            const bool ok = engine.run(line, ctx);
            error = ctx.lastError;
            jm.waitForIdle(10000);
            return ok;
        };
        // Nothing selected, then the dialog when there is no name.
        QVERIFY(!run(QStringLiteral("Archive CREATE")));
        ctx.selection = {src + QStringLiteral("/Game"), src + QStringLiteral("/notes.txt")};
        QVERIFY(run(QStringLiteral("Archive CREATE")));
        QCOMPARE(ctx.tools, 1);
        // QUICK: named after the folder for several items, after the item for one.
        QVERIFY(run(QStringLiteral("Archive CREATE QUICK FORMAT=7z")));
        QVERIFY(QFile::exists(src + QStringLiteral("/src.7z")));
        ctx.selection = {src + QStringLiteral("/notes.txt")};
        QVERIFY(run(QStringLiteral("Archive CREATE QUICK")));
        QVERIFY(QFile::exists(src + QStringLiteral("/notes.zip")));
        QCOMPARE(CommandEngine::suggestedArchiveName({src + QStringLiteral("/Game")}, src, QStringLiteral("tar.gz")),
                 QStringLiteral("Game.tar.gz"));
        // NAME's extension picks the format; FORMAT adds its own when they differ.
        ctx.selection = {src + QStringLiteral("/Game")};
        QVERIFY(run(QStringLiteral("Archive CREATE NAME=\"My Game.tar.xz\" DEST")));
        QVERIFY(QFile::exists(dst + QStringLiteral("/My Game.tar.xz")));
        QVERIFY(run(QStringLiteral("Archive CREATE NAME=Plain DEST")));
        QVERIFY(QFile::exists(dst + QStringLiteral("/Plain.zip")));
        QVERIFY(run(QStringLiteral("Archive CREATE NAME=x FORMAT=tar TO=\"%1\"").arg(dst)));
        QVERIFY(QFile::exists(dst + QStringLiteral("/x.tar")));
        QVERIFY(!run(QStringLiteral("Archive CREATE NAME=x FORMAT=lha")));
        QVERIFY(error.contains(QStringLiteral("FORMAT")));

        // Extract: here (the source folder), to the destination, own folder.
        ctx.source = dst;
        ctx.selection = {dst + QStringLiteral("/My Game.tar.xz"), dst + QStringLiteral("/Plain.zip")};
        const QString out = m_dir->filePath(QStringLiteral("out"));
        QDir().mkpath(out);
        ctx.dest = out;
        QVERIFY(run(QStringLiteral("Archive EXTRACT DEST")));
        QVERIFY(QFile::exists(out + QStringLiteral("/Game/disk1.d64")));
        QVERIFY(QFile::exists(out + QStringLiteral("/Game (2)/disk1.d64")));
        ctx.selection = {dst + QStringLiteral("/Plain.zip")};
        QVERIFY(run(QStringLiteral("Archive EXTRACT MODE=folder")));
        QVERIFY(QFile::exists(dst + QStringLiteral("/Plain/Game/disk1.d64")));
        QVERIFY(!run(QStringLiteral("Archive EXTRACT MODE=sideways")));
        ctx.selection = {src + QStringLiteral("/notes.txt")};
        QVERIFY(!run(QStringLiteral("Archive EXTRACT")));
        QVERIFY(error.contains(QStringLiteral("archive")));
        QVERIFY(!run(QStringLiteral("Archive")));

        // Passwords: zip only when making; asked for when extracting.
        ctx.source = src;
        ctx.selection = {src + QStringLiteral("/notes.txt")};
        QVERIFY(!run(QStringLiteral("Archive CREATE NAME=p.7z PASSWORD=x")));
        QVERIFY(error.contains(QStringLiteral("Only zip")));
        if (run(QStringLiteral("Archive CREATE NAME=p.zip PASSWORD=pw"))) {
            ctx.selection = {src + QStringLiteral("/p.zip")};
            if (Archive::isEncrypted(src + QStringLiteral("/p.zip"))) {
                ctx.passwords = {};
                QVERIFY(!run(QStringLiteral("Archive EXTRACT MODE=folder"))); // asked, canceled
                QCOMPARE(ctx.asked, 1);
                QVERIFY(error.isEmpty());
                QVERIFY(!QFile::exists(src + QStringLiteral("/p")));
                ctx.passwords = {QStringLiteral("pw")};
                QVERIFY(run(QStringLiteral("Archive EXTRACT MODE=folder")));
                QVERIFY(QFile::exists(src + QStringLiteral("/p/notes.txt")));
                QVERIFY(run(QStringLiteral("Archive EXTRACT MODE=folder PASSWORD=pw")));
                QCOMPARE(ctx.asked, 2);
                QVERIFY(QFile::exists(src + QStringLiteral("/p (2)/notes.txt")));
            }
        }
    }

    void extractAndTrash()
    {
        QString error;
        const QString path = src + QStringLiteral("/Pack.zip");
        QVERIFY(Archive::pack({src + QStringLiteral("/Game"), src + QStringLiteral("/notes.txt")}, path, Archive::Format::Zip, {}, &error));
        JobManager jm;
        UndoHistory history(jm);
        JobSpec s;
        s.type = JobType::Unpack;
        s.destDir = src;
        s.sources = {path};
        s.trashAfter = true;
        const int id = jm.enqueue(s);
        QVERIFY(jm.waitForIdle(10000));
        QCOMPARE(jm.snapshot(id).errors, QStringList());
        QVERIFY(!QFile::exists(path));
        QVERIFY(QFile::exists(src + QStringLiteral("/Pack/notes.txt")));
        QTRY_VERIFY(history.latest());
        QVERIFY(history.latest()->title.endsWith(QStringLiteral("and trash it")));
        // Undo: what came out goes, the archive comes back.
        QStringList touched;
        QVERIFY(history.undo(history.latest()->id, &touched, &error));
        QVERIFY(jm.waitForIdle(10000));
        QTRY_VERIFY(QFile::exists(path));
        QTRY_VERIFY(!QFile::exists(src + QStringLiteral("/Pack")));

        // Something failed: the archive stays.
        writeFile(m_dir->filePath(QStringLiteral("broken.zip")), "PK\x03\x04 not really a zip");
        s.sources = {m_dir->filePath(QStringLiteral("broken.zip"))};
        const int bad = jm.enqueue(s);
        QVERIFY(jm.waitForIdle(10000));
        QVERIFY(!jm.snapshot(bad).errors.isEmpty());
        QVERIFY(QFile::exists(m_dir->filePath(QStringLiteral("broken.zip"))));
    }

    void notAnArchive()
    {
        JobManager jm;
        JobSpec s;
        s.type = JobType::Unpack;
        s.destDir = dst;
        s.sources = {src + QStringLiteral("/notes.txt")};
        const int id = jm.enqueue(s);
        QVERIFY(jm.waitForIdle(5000));
        QCOMPARE(jm.snapshot(id).errors.size(), 1);
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QString src;
    QString dst;
};

QTEST_GUILESS_MAIN(TestArchive)
#include "tst_archive.moc"
