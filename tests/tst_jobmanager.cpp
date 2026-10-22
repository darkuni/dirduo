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

JobSpec spec(JobType type, const QStringList &sources, const QString &dest = {})
{
    JobSpec s;
    s.type = type;
    s.sources = sources;
    s.destDir = dest;
    return s;
}

} // namespace

class TestJobManager : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        src = m_dir->filePath(QStringLiteral("src"));
        dst = m_dir->filePath(QStringLiteral("dst"));
        QDir().mkpath(src);
        QDir().mkpath(dst);
        Paths::setDataHomeOverride(m_dir->filePath(QStringLiteral("data")));
    }

    void copiesTreeWithContentAndDates()
    {
        writeFile(src + QStringLiteral("/album/01.mp3"), QByteArray(3 * 1024 * 1024 + 7, 'a'));
        writeFile(src + QStringLiteral("/album/cover.jpg"), "jpg");
        writeFile(src + QStringLiteral("/album/sub/notes.txt"), "notes");
        QVERIFY(QFile::link(QStringLiteral("01.mp3"), src + QStringLiteral("/album/latest")));
        const QDateTime old = QDateTime::currentDateTime().addDays(-100);
        {
            QFile f(src + QStringLiteral("/album/cover.jpg"));
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(old, QFileDevice::FileModificationTime));
        }

        JobManager jm;
        const int id = jm.enqueue(spec(JobType::Copy, {src + QStringLiteral("/album")}, dst));
        QVERIFY(jm.waitForIdle(10000));
        const JobSnapshot s = jm.snapshot(id);
        QCOMPARE(s.state, JobState::Finished);
        QCOMPARE(s.errors, QStringList());
        QCOMPARE(s.doneBytes, s.totalBytes);
        QCOMPARE(readFile(dst + QStringLiteral("/album/01.mp3")).size(), 3 * 1024 * 1024 + 7);
        QCOMPARE(readFile(dst + QStringLiteral("/album/sub/notes.txt")), QByteArray("notes"));
        QCOMPARE(QFileInfo(dst + QStringLiteral("/album/cover.jpg")).lastModified().toSecsSinceEpoch(), old.toSecsSinceEpoch());
        QVERIFY(QFileInfo(dst + QStringLiteral("/album/latest")).isSymLink());
        QCOMPARE(QFileInfo(dst + QStringLiteral("/album/latest")).symLinkTarget(), dst + QStringLiteral("/album/01.mp3"));
        QVERIFY(QFile::exists(src + QStringLiteral("/album/01.mp3"))); // copy keeps the source
        QVERIFY(!QFile::exists(dst + QStringLiteral("/album/01.mp3.linopus-part")));
    }

    void copyIntoSameFolderMakesACopy()
    {
        writeFile(src + QStringLiteral("/a.txt"), "1");
        JobManager jm;
        jm.enqueue(spec(JobType::Copy, {src + QStringLiteral("/a.txt")}, src));
        QVERIFY(jm.waitForIdle(5000));
        QCOMPARE(readFile(src + QStringLiteral("/a (2).txt")), QByteArray("1"));
    }

    void refusesFolderIntoItself()
    {
        QDir().mkpath(src + QStringLiteral("/f/inner"));
        JobManager jm;
        const int id = jm.enqueue(spec(JobType::Copy, {src + QStringLiteral("/f")}, src + QStringLiteral("/f/inner")));
        QVERIFY(jm.waitForIdle(5000));
        QCOMPARE(jm.snapshot(id).state, JobState::Failed);
        QVERIFY(jm.snapshot(id).errors.first().contains(QStringLiteral("into itself")));
    }

    void collisionAskSkipReplaceRename()
    {
        writeFile(src + QStringLiteral("/one.txt"), "new1");
        writeFile(src + QStringLiteral("/two.txt"), "new2");
        writeFile(src + QStringLiteral("/three.txt"), "new3");
        writeFile(dst + QStringLiteral("/one.txt"), "old1");
        writeFile(dst + QStringLiteral("/two.txt"), "old2");
        writeFile(dst + QStringLiteral("/three.txt"), "old3");

        JobManager jm;
        QStringList asked;
        connect(&jm, &JobManager::collisionRequested, this, [&](const CollisionRequest &r) {
            asked << QFileInfo(r.source).fileName();
            CollisionDecision d;
            if (r.source.endsWith(QLatin1String("one.txt")))
                d.action = CollisionAction::Skip;
            else if (r.source.endsWith(QLatin1String("two.txt")))
                d.action = CollisionAction::Replace;
            else {
                d.action = CollisionAction::Rename;
                d.newName = QStringLiteral("three-renamed.txt");
            }
            jm.resolveCollision(r.jobId, d);
        });
        jm.enqueue(spec(JobType::Copy,
                        {src + QStringLiteral("/one.txt"), src + QStringLiteral("/two.txt"), src + QStringLiteral("/three.txt")},
                        dst));
        QVERIFY(jm.waitForIdle(5000));
        QCOMPARE(asked.size(), 3);
        QCOMPARE(readFile(dst + QStringLiteral("/one.txt")), QByteArray("old1"));
        QCOMPARE(readFile(dst + QStringLiteral("/two.txt")), QByteArray("new2"));
        QCOMPARE(readFile(dst + QStringLiteral("/three.txt")), QByteArray("old3"));
        QCOMPARE(readFile(dst + QStringLiteral("/three-renamed.txt")), QByteArray("new3"));
    }

    void applyToAllAsksOnce()
    {
        for (int i = 0; i < 5; ++i) {
            writeFile(src + QStringLiteral("/f%1").arg(i), "new");
            writeFile(dst + QStringLiteral("/f%1").arg(i), "old");
        }
        JobManager jm;
        int asks = 0;
        connect(&jm, &JobManager::collisionRequested, this, [&](const CollisionRequest &r) {
            ++asks;
            CollisionDecision d;
            d.action = CollisionAction::Rename;
            d.applyToAll = true;
            jm.resolveCollision(r.jobId, d);
        });
        QStringList sources;
        for (int i = 0; i < 5; ++i)
            sources << src + QStringLiteral("/f%1").arg(i);
        jm.enqueue(spec(JobType::Copy, sources, dst));
        QVERIFY(jm.waitForIdle(5000));
        QCOMPARE(asks, 1);
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(readFile(dst + QStringLiteral("/f%1").arg(i)), QByteArray("old"));
            QCOMPARE(readFile(dst + QStringLiteral("/f%1 (2)").arg(i)), QByteArray("new"));
        }
    }

    void whenExistsNewer()
    {
        writeFile(src + QStringLiteral("/older"), "src-older");
        writeFile(src + QStringLiteral("/newer"), "src-newer");
        writeFile(dst + QStringLiteral("/older"), "dst");
        writeFile(dst + QStringLiteral("/newer"), "dst");
        auto stamp = [](const QString &p, int days) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(QDateTime::currentDateTime().addDays(days), QFileDevice::FileModificationTime));
        };
        stamp(src + QStringLiteral("/older"), -10);
        stamp(src + QStringLiteral("/newer"), -1);
        stamp(dst + QStringLiteral("/older"), -5);
        stamp(dst + QStringLiteral("/newer"), -5);

        JobManager jm;
        JobSpec s = spec(JobType::Copy, {src + QStringLiteral("/older"), src + QStringLiteral("/newer")}, dst);
        s.whenExists = CollisionAction::ReplaceIfNewer;
        jm.enqueue(s);
        QVERIFY(jm.waitForIdle(5000));
        QCOMPARE(readFile(dst + QStringLiteral("/older")), QByteArray("dst"));
        QCOMPARE(readFile(dst + QStringLiteral("/newer")), QByteArray("src-newer"));
    }

    void foldersMerge()
    {
        writeFile(src + QStringLiteral("/music/a.mp3"), "a");
        writeFile(dst + QStringLiteral("/music/b.mp3"), "b");
        JobManager jm;
        const int id = jm.enqueue(spec(JobType::Copy, {src + QStringLiteral("/music")}, dst));
        QVERIFY(jm.waitForIdle(5000));
        QCOMPARE(jm.snapshot(id).state, JobState::Finished);
        QVERIFY(QFile::exists(dst + QStringLiteral("/music/a.mp3")));
        QVERIFY(QFile::exists(dst + QStringLiteral("/music/b.mp3")));
    }

    void moveSameDeviceAndMergeMove()
    {
        writeFile(src + QStringLiteral("/whole/x.txt"), "x");
        writeFile(src + QStringLiteral("/merge/y.txt"), "y");
        writeFile(dst + QStringLiteral("/merge/z.txt"), "z");
        JobManager jm;
        const int id = jm.enqueue(spec(JobType::Move, {src + QStringLiteral("/whole"), src + QStringLiteral("/merge")}, dst));
        QVERIFY(jm.waitForIdle(5000));
        QCOMPARE(jm.snapshot(id).errors, QStringList());
        QVERIFY(!QFile::exists(src + QStringLiteral("/whole")));
        QVERIFY(!QFile::exists(src + QStringLiteral("/merge"))); // emptied, then removed
        QCOMPARE(readFile(dst + QStringLiteral("/whole/x.txt")), QByteArray("x"));
        QCOMPARE(readFile(dst + QStringLiteral("/merge/y.txt")), QByteArray("y"));
        QCOMPARE(readFile(dst + QStringLiteral("/merge/z.txt")), QByteArray("z"));
    }

    void moveKeepsFolderWhenSomethingSkipped()
    {
        writeFile(src + QStringLiteral("/m/keep.txt"), "new");
        writeFile(src + QStringLiteral("/m/go.txt"), "go");
        writeFile(dst + QStringLiteral("/m/keep.txt"), "old");
        JobManager jm;
        JobSpec s = spec(JobType::Move, {src + QStringLiteral("/m")}, dst);
        s.whenExists = CollisionAction::Skip;
        jm.enqueue(s);
        QVERIFY(jm.waitForIdle(5000));
        QVERIFY(QFile::exists(src + QStringLiteral("/m/keep.txt")));
        QVERIFY(!QFile::exists(src + QStringLiteral("/m/go.txt")));
        QCOMPARE(readFile(dst + QStringLiteral("/m/keep.txt")), QByteArray("old"));
    }

    void copyAsUsesNewNames()
    {
        writeFile(src + QStringLiteral("/IMG0001.JPG"), "pic");
        JobManager jm;
        JobSpec s = spec(JobType::Copy, {src + QStringLiteral("/IMG0001.JPG")}, dst);
        s.targetNames.insert(src + QStringLiteral("/IMG0001.JPG"), QStringLiteral("Disneyland0001.jpg"));
        jm.enqueue(s);
        QVERIFY(jm.waitForIdle(5000));
        QCOMPARE(readFile(dst + QStringLiteral("/Disneyland0001.jpg")), QByteArray("pic"));
    }

    void sameDeviceJobsQueueInOrder()
    {
        writeFile(src + QStringLiteral("/big.bin"), QByteArray(8 * 1024 * 1024, 'b'));
        writeFile(src + QStringLiteral("/small.txt"), "s");
        QDir().mkpath(dst + QStringLiteral("/2"));
        JobManager jm;
        const int first = jm.enqueue(spec(JobType::Copy, {src + QStringLiteral("/big.bin")}, dst));
        jm.pause(first);
        const int second = jm.enqueue(spec(JobType::Copy, {src + QStringLiteral("/small.txt")}, dst + QStringLiteral("/2")));
        QTest::qWait(300);
        // Same device: the second job waits behind the paused first one.
        QCOMPARE(jm.snapshot(second).state, JobState::Queued);
        QVERIFY(!QFile::exists(dst + QStringLiteral("/2/small.txt")));

        jm.startNow(second); // jump the queue
        QTRY_VERIFY_WITH_TIMEOUT(jm.snapshot(second).isDone(), 5000);
        QVERIFY(QFile::exists(dst + QStringLiteral("/2/small.txt")));
        QVERIFY(!jm.snapshot(first).isDone());

        jm.resume(first);
        QVERIFY(jm.waitForIdle(10000));
        QCOMPARE(jm.snapshot(first).state, JobState::Finished);
        QCOMPARE(QFileInfo(dst + QStringLiteral("/big.bin")).size(), 8 * 1024 * 1024);
    }

    void cancelQueuedAndRunning()
    {
        writeFile(src + QStringLiteral("/big.bin"), QByteArray(16 * 1024 * 1024, 'b'));
        JobManager jm;
        const int first = jm.enqueue(spec(JobType::Copy, {src + QStringLiteral("/big.bin")}, dst));
        jm.pause(first);
        const int second = jm.enqueue(spec(JobType::Copy, {src + QStringLiteral("/big.bin")}, dst));
        jm.cancel(second);
        QCOMPARE(jm.snapshot(second).state, JobState::Cancelled);
        jm.cancel(first);
        QVERIFY(jm.waitForIdle(5000));
        QCOMPARE(jm.snapshot(first).state, JobState::Cancelled);
        QVERIFY(!QFile::exists(dst + QStringLiteral("/big.bin.linopus-part")));
    }

    void trashAndDelete()
    {
        writeFile(src + QStringLiteral("/t.txt"), "t");
        writeFile(src + QStringLiteral("/d/inner/x"), "x");
        JobManager jm;
        const int t = jm.enqueue(spec(JobType::Trash, {src + QStringLiteral("/t.txt")}));
        const int d = jm.enqueue(spec(JobType::Delete, {src + QStringLiteral("/d")}));
        QVERIFY(jm.waitForIdle(5000));
        QCOMPARE(jm.snapshot(t).state, JobState::Finished);
        QCOMPARE(jm.snapshot(d).state, JobState::Finished);
        QVERIFY(!QFile::exists(src + QStringLiteral("/t.txt")));
        QVERIFY(!QFile::exists(src + QStringLiteral("/d")));
    }

    void noListenerSkipsCollisions()
    {
        writeFile(src + QStringLiteral("/c.txt"), "new");
        writeFile(dst + QStringLiteral("/c.txt"), "old");
        JobManager jm;
        jm.enqueue(spec(JobType::Copy, {src + QStringLiteral("/c.txt")}, dst));
        QVERIFY(jm.waitForIdle(5000));
        QCOMPARE(readFile(dst + QStringLiteral("/c.txt")), QByteArray("old"));
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QString src;
    QString dst;
};

QTEST_GUILESS_MAIN(TestJobManager)
#include "tst_jobmanager.moc"
