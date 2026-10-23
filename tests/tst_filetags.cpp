#include "core/AliasManager.h"
#include "core/CommandEngine.h"
#include "core/FileTags.h"
#include "core/JobManager.h"
#include "core/Paths.h"
#include "core/SelectRules.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <sys/xattr.h>

namespace {

void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
}

class Ctx : public CommandContext
{
public:
    QString source;
    QStringList selection;
    QStringList refreshed;
    QString sourcePath() const override { return source; }
    QString destPath() const override { return {}; }
    QStringList selectedPaths() const override { return selection; }
    QString focusedPath() const override { return selection.value(0); }
    bool promptText(const QString &, const QString &, QString &) override { return false; }
    bool confirm(const QString &, const QString &) override { return true; }
    QString lastError;
    void showError(const QString &, const QString &t) override { lastError = t; }
    QString chooseDestination(const QString &) override { return {}; }
    bool go(const ParsedCommand &, QString *) override { return true; }
    bool set(const ParsedCommand &, QString *) override { return true; }
    bool select(const ParsedCommand &, QString *) override { return true; }
    bool beginInlineRename() override { return true; }
    void afterLocalChange(const QStringList &p) override { refreshed = p; }
};

} // namespace

class TestFileTags : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        Paths::setConfigDirOverride(m_dir->filePath(QStringLiteral("cfg")));
        Paths::setDataHomeOverride(m_dir->filePath(QStringLiteral("data")));
        FileTags::setFallbackFile(m_dir->filePath(QStringLiteral("tags.json")));
        FileTags::setForceFallback(false);
        touch(m_dir->filePath(QStringLiteral("a.txt")));
    }

    void onTheFileLikeDolphin()
    {
        const QString f = m_dir->filePath(QStringLiteral("a.txt"));
        if (!FileTags::storedOnFile(f))
            QSKIP("This filesystem keeps no extended attributes");
        FileTagData d;
        d.label = QStringLiteral("Red");
        d.rating = 4;
        d.tags = {QStringLiteral("c64"), QStringLiteral("to fix")};
        d.comment = QStringLiteral("Dumped from the gray cart");
        QVERIFY(FileTags::write(f, d));
        QCOMPARE(FileTags::read(f), d);
        // The same attributes Dolphin and Baloo read.
        char buf[64];
        ssize_t n = ::getxattr(QFile::encodeName(f).constData(), "user.baloo.rating", buf, sizeof(buf));
        QCOMPARE(QByteArray(buf, int(n)), QByteArray("8"));
        n = ::getxattr(QFile::encodeName(f).constData(), "user.xdg.tags", buf, sizeof(buf));
        QCOMPARE(QByteArray(buf, int(n)), QByteArray("c64,to fix"));
        // Clearing removes them.
        QVERIFY(FileTags::write(f, FileTagData{}));
        QVERIFY(FileTags::read(f).isEmpty());
        QVERIFY(::getxattr(QFile::encodeName(f).constData(), "user.xdg.tags", buf, sizeof(buf)) < 0);
        QCOMPARE(FileTags::stars(3), QStringLiteral("★★★☆☆"));
        QVERIFY(FileTags::knownTags().contains(QStringLiteral("c64")));
    }

    void fallbackListFollowsRenames()
    {
        FileTags::setForceFallback(true);
        const QString f = m_dir->filePath(QStringLiteral("card/game.d64"));
        touch(f);
        FileTagData d;
        d.rating = 2;
        QVERIFY(FileTags::write(f, d));
        QCOMPARE(FileTags::read(f).rating, 2);
        QVERIFY(QFile::exists(m_dir->filePath(QStringLiteral("tags.json"))));
        FileTags::renamed(m_dir->filePath(QStringLiteral("card")), m_dir->filePath(QStringLiteral("card2")));
        QCOMPARE(FileTags::read(m_dir->filePath(QStringLiteral("card2/game.d64"))).rating, 2);
        QVERIFY(FileTags::read(f).isEmpty());
        FileTags::setForceFallback(false);
    }

    void copiesKeepThem()
    {
        const QString f = m_dir->filePath(QStringLiteral("a.txt"));
        if (!FileTags::storedOnFile(f))
            QSKIP("This filesystem keeps no extended attributes");
        FileTagData d;
        d.label = QStringLiteral("Green");
        d.tags = {QStringLiteral("keep")};
        QVERIFY(FileTags::write(f, d));
        const QString dst = m_dir->filePath(QStringLiteral("dst"));
        QDir().mkpath(dst);
        JobManager jm;
        JobSpec s;
        s.type = JobType::Copy;
        s.sources = {f};
        s.destDir = dst;
        jm.enqueue(s);
        QVERIFY(jm.waitForIdle(5000));
        QCOMPARE(FileTags::read(dst + QStringLiteral("/a.txt")), d);
    }

    void tagCommandAndUndo()
    {
        AliasManager aliases(m_dir->filePath(QStringLiteral("aliases.ini")));
        JobManager jm;
        CommandEngine engine(aliases, jm);
        Ctx ctx;
        ctx.source = m_dir->path();
        const QString a = m_dir->filePath(QStringLiteral("a.txt"));
        const QString b = m_dir->filePath(QStringLiteral("b.txt"));
        touch(b);
        ctx.selection = {a, b};
        QVERIFY(engine.run(QStringLiteral("Tag LABEL=red RATING=3 ADD=\"c64, demo\""), ctx));
        QCOMPARE(FileTags::read(a).label, QStringLiteral("Red")); // the label's own spelling
        QCOMPARE(FileTags::read(b).rating, 3);
        QCOMPARE(FileTags::read(b).tags, (QStringList{QStringLiteral("c64"), QStringLiteral("demo")}));
        QCOMPARE(ctx.refreshed, ctx.selection);
        QVERIFY(engine.run(QStringLiteral("Tag RATING=+ REMOVE=demo"), ctx));
        QCOMPARE(FileTags::read(a).rating, 4);
        QCOMPARE(FileTags::read(a).tags, QStringList{QStringLiteral("c64")});
        QVERIFY(engine.run(QStringLiteral("Tag COMMENT=\"Side B\""), ctx));
        QCOMPARE(FileTags::read(b).comment, QStringLiteral("Side B"));
        QVERIFY(!engine.run(QStringLiteral("Tag LABEL=Magenta"), ctx));
        QVERIFY(!engine.run(QStringLiteral("Tag RATING=9"), ctx));
        // Undo puts the description back as it was (and is itself undoable).
        QVERIFY(engine.run(QStringLiteral("Undo"), ctx));
        QVERIFY(FileTags::read(b).comment.isEmpty());
        QCOMPARE(FileTags::read(a).rating, 4);
        QVERIFY(engine.run(QStringLiteral("Undo"), ctx)); // redo
        QCOMPARE(FileTags::read(b).comment, QStringLiteral("Side B"));
        QVERIFY(engine.run(QStringLiteral("Tag CLEARALL"), ctx));
        QVERIFY(FileTags::read(a).isEmpty());

        // Selecting by them.
        FileTagData red;
        red.label = QStringLiteral("Red");
        red.rating = 5;
        red.tags = {QStringLiteral("demo")};
        FileItem it;
        it.tags = red;
        SelectRules r;
        r.label = QStringLiteral("red");
        QVERIFY(r.matches(it));
        r.label = QStringLiteral("none");
        QVERIFY(!r.matches(it));
        r = SelectRules();
        r.minRating = 4;
        QVERIFY(r.matches(it));
        r.minRating = 0;
        r.maxRating = 0;
        QVERIFY(!r.matches(it));
        r = SelectRules();
        r.tag = QStringLiteral("DEMO");
        QVERIFY(r.matches(it));
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
};

QTEST_GUILESS_MAIN(TestFileTags)
#include "tst_filetags.moc"
