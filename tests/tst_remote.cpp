// Remote locations through KIO, against real servers on localhost.
// Run through tests/remote/run-remote-tests.sh, which starts an FTP server and
// an SSH server and sets the LINOPUS_TEST_* variables. Without them every
// test is skipped.

#include "core/AliasManager.h"
#include "core/CommandEngine.h"
#include "core/DirLister.h"
#include "core/JobManager.h"
#include "core/Location.h"
#include "core/Paths.h"
#include "kio/KioBackend.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

class Ctx : public CommandContext
{
public:
    QString source;
    QStringList selection;
    QStringList errors;
    QStringList changed;
    QString sourcePath() const override { return source; }
    QString destPath() const override { return {}; }
    QStringList selectedPaths() const override { return selection; }
    QString focusedPath() const override { return selection.value(0); }
    bool promptText(const QString &, const QString &, QString &) override { return false; }
    bool confirm(const QString &, const QString &) override { return true; }
    void showError(const QString &t, const QString &e) override { errors << t + QStringLiteral(": ") + e; }
    QString chooseDestination(const QString &) override { return {}; }
    bool go(const ParsedCommand &, QString *) override { return true; }
    bool set(const ParsedCommand &, QString *) override { return true; }
    bool select(const ParsedCommand &, QString *) override { return true; }
    bool beginInlineRename() override { return true; }
    void afterLocalChange(const QStringList &paths) override { changed = paths; }
};

} // namespace

class TestRemote : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        ftp = qEnvironmentVariable("LINOPUS_TEST_FTP");
        ftpDir = qEnvironmentVariable("LINOPUS_TEST_FTP_DIR");
        sftp = qEnvironmentVariable("LINOPUS_TEST_SFTP");
        sftpDir = qEnvironmentVariable("LINOPUS_TEST_SFTP_DIR");
        if (ftp.isEmpty() || sftp.isEmpty())
            QSKIP("No test servers; run tests/remote/run-remote-tests.sh");
        QVERIFY(m_local.isValid());
        Paths::setConfigDirOverride(m_local.filePath(QStringLiteral("config")));
        Paths::setDataHomeOverride(m_local.filePath(QStringLiteral("data")));
        m_kio = std::make_unique<KioBackend>();
    }

    void init()
    {
        m_jobs = std::make_unique<JobManager>();
        local = m_local.filePath(QStringLiteral("local-%1").arg(++m_round));
        QDir().mkpath(local);
    }

    // ---------------------------------------------------------- listing

    void ftpListsFolder()
    {
        writeFile(ftpDir + QStringLiteral("/list/one.txt"), "1");
        writeFile(ftpDir + QStringLiteral("/list/sub folder/two.txt"), "22");
        const auto items = list(ftp + QStringLiteral("/list"));
        QCOMPARE(names(items), (QStringList{QStringLiteral("one.txt"), QStringLiteral("sub folder")}));
        for (const FileItem &i : items) {
            if (i.name == QLatin1String("sub folder"))
                QVERIFY(i.isDir);
            else
                QCOMPARE(i.size, 1);
        }
        // Item locations are full remote locations, usable as-is.
        for (const FileItem &i : items)
            QVERIFY(i.path.startsWith(QStringLiteral("ftp://")));
    }

    void sftpListsFolder()
    {
        writeFile(sftpDir + QStringLiteral("/list/a.bin"), QByteArray(3000, 'a'));
        QDir().mkpath(sftpDir + QStringLiteral("/list/dir"));
        const auto items = list(sftp + sftpDir + QStringLiteral("/list"));
        QCOMPARE(names(items), (QStringList{QStringLiteral("a.bin"), QStringLiteral("dir")}));
    }

    void missingFolderFails()
    {
        DirLister lister;
        QSignalSpy failed(&lister, &DirLister::failed);
        lister.open(ftp + QStringLiteral("/does-not-exist"));
        QVERIFY(failed.wait(15000));
        QVERIFY(!failed.first().at(1).toString().isEmpty());
    }

    // ---------------------------------------------------------- copying

    void ftpUploadAndDownload()
    {
        writeFile(local + QStringLiteral("/up/song.mp3"), QByteArray(200000, 's'));
        QDir().mkpath(ftpDir + QStringLiteral("/in"));
        runCopy({local + QStringLiteral("/up")}, ftp + QStringLiteral("/in"), JobType::Copy);
        QCOMPARE(readFile(ftpDir + QStringLiteral("/in/up/song.mp3")).size(), 200000);

        const QString back = local + QStringLiteral("/back");
        QDir().mkpath(back);
        const JobSnapshot s = runCopy({ftp + QStringLiteral("/in/up/song.mp3")}, back, JobType::Copy);
        QCOMPARE(readFile(back + QStringLiteral("/song.mp3")).size(), 200000);
        QCOMPARE(s.doneBytes, s.totalBytes);
    }

    void sftpUploadAndDownload()
    {
        writeFile(local + QStringLiteral("/roms/c64/Galaga.d64"), QByteArray(174848, 'g'));
        QDir().mkpath(sftpDir + QStringLiteral("/up"));
        runCopy({local + QStringLiteral("/roms")}, sftp + sftpDir + QStringLiteral("/up"), JobType::Copy);
        QCOMPARE(readFile(sftpDir + QStringLiteral("/up/roms/c64/Galaga.d64")).size(), 174848);

        runCopy({sftp + sftpDir + QStringLiteral("/up/roms/c64/Galaga.d64")}, local, JobType::Copy);
        QCOMPARE(readFile(local + QStringLiteral("/Galaga.d64")).size(), 174848);
    }

    void moveFromServerRemovesOriginal()
    {
        writeFile(ftpDir + QStringLiteral("/mv/take.txt"), "take");
        runCopy({ftp + QStringLiteral("/mv/take.txt")}, local, JobType::Move);
        QCOMPARE(readFile(local + QStringLiteral("/take.txt")), QByteArray("take"));
        QVERIFY(!QFile::exists(ftpDir + QStringLiteral("/mv/take.txt")));
    }

    void copyAsToServer()
    {
        writeFile(local + QStringLiteral("/IMG0001.JPG"), "pic");
        QDir().mkpath(sftpDir + QStringLiteral("/as"));
        JobSpec spec;
        spec.type = JobType::Copy;
        spec.sources = {local + QStringLiteral("/IMG0001.JPG")};
        spec.destDir = sftp + sftpDir + QStringLiteral("/as");
        spec.targetNames.insert(local + QStringLiteral("/IMG0001.JPG"), QStringLiteral("Disneyland0001.jpg"));
        run(spec);
        QCOMPARE(readFile(sftpDir + QStringLiteral("/as/Disneyland0001.jpg")), QByteArray("pic"));
    }

    // ---------------------------------------------------------- collisions

    void collisionsUseOurDialog()
    {
        for (const char *n : {"skip.txt", "replace.txt", "rename.txt"}) {
            writeFile(local + QStringLiteral("/c/") + QLatin1String(n), "new");
            writeFile(ftpDir + QStringLiteral("/c/") + QLatin1String(n), "old");
        }
        QStringList asked;
        connect(m_jobs.get(), &JobManager::collisionRequested, this, [&](const CollisionRequest &r) {
            asked << Location::fileName(r.target);
            QVERIFY(r.target.startsWith(QStringLiteral("ftp://")));
            QVERIFY(!r.suggestedName.isEmpty());
            CollisionDecision d;
            if (r.source.endsWith(QLatin1String("skip.txt")))
                d.action = CollisionAction::Skip;
            else if (r.source.endsWith(QLatin1String("replace.txt")))
                d.action = CollisionAction::Replace;
            else {
                d.action = CollisionAction::Rename;
                d.newName = QStringLiteral("renamed.txt");
            }
            m_jobs->resolveCollision(r.jobId, d);
        });
        runCopy({local + QStringLiteral("/c/skip.txt"), local + QStringLiteral("/c/replace.txt"),
                 local + QStringLiteral("/c/rename.txt")},
                ftp + QStringLiteral("/c"), JobType::Copy);
        QCOMPARE(asked.size(), 3);
        QCOMPARE(readFile(ftpDir + QStringLiteral("/c/skip.txt")), QByteArray("old"));
        QCOMPARE(readFile(ftpDir + QStringLiteral("/c/replace.txt")), QByteArray("new"));
        QCOMPARE(readFile(ftpDir + QStringLiteral("/c/rename.txt")), QByteArray("old"));
        QCOMPARE(readFile(ftpDir + QStringLiteral("/c/renamed.txt")), QByteArray("new"));
    }

    void applyToAllAsksOnce()
    {
        QStringList sources;
        for (int i = 0; i < 4; ++i) {
            writeFile(local + QStringLiteral("/all/f%1").arg(i), "new");
            writeFile(sftpDir + QStringLiteral("/all/f%1").arg(i), "old");
            sources << local + QStringLiteral("/all/f%1").arg(i);
        }
        int asks = 0;
        connect(m_jobs.get(), &JobManager::collisionRequested, this, [&](const CollisionRequest &r) {
            ++asks;
            CollisionDecision d;
            d.action = CollisionAction::Replace;
            d.applyToAll = true;
            m_jobs->resolveCollision(r.jobId, d);
        });
        runCopy(sources, sftp + sftpDir + QStringLiteral("/all"), JobType::Copy);
        QCOMPARE(asks, 1);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(readFile(sftpDir + QStringLiteral("/all/f%1").arg(i)), QByteArray("new"));
    }

    void whenExistsSkipNeedsNoDialog()
    {
        writeFile(local + QStringLiteral("/w/x.txt"), "new");
        writeFile(ftpDir + QStringLiteral("/w/x.txt"), "old");
        int asks = 0;
        connect(m_jobs.get(), &JobManager::collisionRequested, this, [&](const CollisionRequest &) { ++asks; });
        JobSpec spec;
        spec.type = JobType::Copy;
        spec.sources = {local + QStringLiteral("/w/x.txt")};
        spec.destDir = ftp + QStringLiteral("/w");
        spec.whenExists = CollisionAction::Skip;
        run(spec);
        QCOMPARE(asks, 0);
        QCOMPARE(readFile(ftpDir + QStringLiteral("/w/x.txt")), QByteArray("old"));
    }

    // ---------------------------------------------------------- other operations

    void deleteOnServer()
    {
        writeFile(sftpDir + QStringLiteral("/del/a/b.txt"), "b");
        JobSpec spec;
        spec.type = JobType::Delete;
        spec.sources = {sftp + sftpDir + QStringLiteral("/del/a")};
        const JobSnapshot s = run(spec);
        QCOMPARE(s.state, JobState::Finished);
        QVERIFY(!QFile::exists(sftpDir + QStringLiteral("/del/a")));
    }

    void cancelStopsTransfer()
    {
        writeFile(local + QStringLiteral("/big.bin"), QByteArray(64 * 1024 * 1024, 'b'));
        QDir().mkpath(sftpDir + QStringLiteral("/big"));
        JobSpec spec;
        spec.type = JobType::Copy;
        spec.sources = {local + QStringLiteral("/big.bin")};
        spec.destDir = sftp + sftpDir + QStringLiteral("/big");
        const int id = m_jobs->enqueue(spec);
        QTRY_VERIFY_WITH_TIMEOUT(m_jobs->snapshot(id).doneBytes > 0, 15000);
        m_jobs->cancel(id);
        QVERIFY(m_jobs->waitForIdle(15000));
        QCOMPARE(m_jobs->snapshot(id).state, JobState::Cancelled);
    }

    void serverJobsQueueTogether()
    {
        JobSpec a;
        a.type = JobType::Copy;
        a.destDir = sftp + sftpDir + QStringLiteral("/q1");
        JobSpec b = a;
        b.destDir = sftp + sftpDir + QStringLiteral("/q2");
        QCOMPARE(JobManager::queueKeyFor(a), JobManager::queueKeyFor(b));
        b.destDir = ftp + QStringLiteral("/q");
        QVERIFY(JobManager::queueKeyFor(a) != JobManager::queueKeyFor(b));
    }

    void renameAndMkdirOnServer()
    {
        QString error;
        QVERIFY2(m_kio->mkdir(ftp + QStringLiteral("/made"), &error), qPrintable(error));
        QVERIFY(QFileInfo(ftpDir + QStringLiteral("/made")).isDir());
        writeFile(ftpDir + QStringLiteral("/made/old.txt"), "x");
        QVERIFY2(m_kio->rename(ftp + QStringLiteral("/made/old.txt"), ftp + QStringLiteral("/made/new.txt"), &error),
                 qPrintable(error));
        QVERIFY(QFile::exists(ftpDir + QStringLiteral("/made/new.txt")));
        QVERIFY(!m_kio->mkdir(ftp + QStringLiteral("/made"), &error)); // exists already
    }

    void commandsWorkOnServers()
    {
        AliasManager aliases(m_local.filePath(QStringLiteral("aliases.ini")));
        CommandEngine engine(aliases, *m_jobs);
        Ctx ctx;
        ctx.source = sftp + sftpDir;
        QVERIFY2(engine.run(QStringLiteral("CreateFolder NAME=\"Cmd A, Cmd B\""), ctx), qPrintable(ctx.errors.join(QLatin1Char('\n'))));
        QVERIFY(QFileInfo(sftpDir + QStringLiteral("/Cmd A")).isDir());
        QCOMPARE(ctx.changed.size(), 2);

        writeFile(sftpDir + QStringLiteral("/House_03e12.MPG"), "ep");
        QVERIFY(engine.run(QStringLiteral("Rename FROM=House_03e12.MPG TO=HouseMD_3x12.MPG"), ctx));
        QVERIFY(QFile::exists(sftpDir + QStringLiteral("/HouseMD_3x12.MPG")));

        // Plain Delete on a server is permanent (after a confirmation).
        ctx.selection = {sftp + sftpDir + QStringLiteral("/Cmd B")};
        QVERIFY(engine.run(QStringLiteral("Delete"), ctx));
        QVERIFY(m_jobs->waitForIdle(15000));
        QVERIFY(!QFileInfo::exists(sftpDir + QStringLiteral("/Cmd B")));
    }

    void aliasesCanPointAtServers()
    {
        AliasManager aliases(m_local.filePath(QStringLiteral("aliases2.ini")));
        QString error;
        QVERIFY2(aliases.setUserAlias(QStringLiteral("nas"), sftp + sftpDir, &error), qPrintable(error));
        QCOMPARE(aliases.resolve(QStringLiteral("/nas/roms/c64")), Location::join(sftp + sftpDir, QStringLiteral("roms/c64")));
        QCOMPARE(aliases.resolve(QStringLiteral("/nas")), Location::clean(sftp + sftpDir));
    }

    // ---------------------------------------------------------- archives

    void archivesOpenAsFolders()
    {
        const QString zip = local + QStringLiteral("/roms.zip");
        QProcess py;
        py.start(QStringLiteral("python3"),
                 {QStringLiteral("-c"),
                  QStringLiteral("import zipfile,sys\n"
                                 "z=zipfile.ZipFile(sys.argv[1],'w')\n"
                                 "z.writestr('c64/Galaga.d64', b'g'*1000)\n"
                                 "z.writestr('c64/Elite.d64', b'e'*500)\n"
                                 "z.writestr('readme.txt', b'hello')\n"
                                 "z.close()"),
                  zip});
        QVERIFY(py.waitForFinished(10000));
        QCOMPARE(py.exitCode(), 0);

        const QString inside = m_kio->archiveLocation(zip);
        QVERIFY2(!inside.isEmpty(), "zip should open as a folder");
        QVERIFY(Location::isArchive(inside));
        QCOMPARE(Location::archiveFile(inside), zip);
        QCOMPARE(names(list(inside)), (QStringList{QStringLiteral("c64"), QStringLiteral("readme.txt")}));
        QCOMPARE(names(list(Location::join(inside, QStringLiteral("c64")))),
                 (QStringList{QStringLiteral("Elite.d64"), QStringLiteral("Galaga.d64")}));
        QCOMPARE(Location::parent(inside), local); // Up leaves the archive

        // Copy out of the archive.
        const QString out = local + QStringLiteral("/extracted");
        QDir().mkpath(out);
        runCopy({Location::join(inside, QStringLiteral("c64/Galaga.d64"))}, out, JobType::Copy);
        QCOMPARE(readFile(out + QStringLiteral("/Galaga.d64")).size(), 1000);

        QVERIFY(m_kio->archiveLocation(local + QStringLiteral("/extracted/Galaga.d64")).isEmpty());
    }

private:
    QStringList names(const FileItemList &items)
    {
        QStringList out;
        for (const FileItem &i : items)
            out << i.name;
        out.sort();
        return out;
    }

    FileItemList list(const QString &loc)
    {
        DirLister lister;
        QSignalSpy listed(&lister, &DirLister::listed);
        QSignalSpy failed(&lister, &DirLister::failed);
        lister.open(loc);
        for (int i = 0; i < 150 && listed.isEmpty() && failed.isEmpty(); ++i)
            QTest::qWait(100);
        if (!failed.isEmpty()) {
            qWarning() << "listing" << loc << "failed:" << failed.first().at(1).toString();
            return {};
        }
        if (listed.isEmpty()) {
            qWarning() << "listing" << loc << "timed out";
            return {};
        }
        return listed.last().at(1).value<FileItemList>();
    }

    JobSnapshot run(const JobSpec &spec)
    {
        const int id = m_jobs->enqueue(spec);
        if (!m_jobs->waitForIdle(30000))
            qWarning() << "job" << id << "did not finish";
        const JobSnapshot s = m_jobs->snapshot(id);
        if (!s.errors.isEmpty())
            qWarning() << "job errors:" << s.errors;
        return s;
    }

    JobSnapshot runCopy(const QStringList &sources, const QString &dest, JobType type)
    {
        JobSpec spec;
        spec.type = type;
        spec.sources = sources;
        spec.destDir = dest;
        const JobSnapshot s = run(spec);
        [&]() { QCOMPARE(s.state, JobState::Finished); }();
        return s;
    }

    QString ftp, ftpDir, sftp, sftpDir, local;
    QTemporaryDir m_local;
    int m_round = 0;
    std::unique_ptr<KioBackend> m_kio;
    std::unique_ptr<JobManager> m_jobs;
};

QTEST_MAIN(TestRemote)
#include "tst_remote.moc"
