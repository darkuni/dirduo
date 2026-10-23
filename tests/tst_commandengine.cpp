#include "core/AliasManager.h"
#include "core/CommandEngine.h"
#include "core/HostRun.h"
#include "core/JobManager.h"
#include "core/Paths.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTest>

namespace {

void writeFile(const QString &path, const QByteArray &data = "x")
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

class FakeContext : public CommandContext
{
public:
    QString source;
    QString dest;
    QStringList selection;
    QStringList promptAnswers;
    bool confirmAnswer = true;
    QStringList errors;
    QStringList goCalls;
    QStringList setCalls;
    QStringList selected; // from afterLocalChange
    int inlineRenames = 0;

    QString sourcePath() const override { return source; }
    QString destPath() const override { return dest; }
    QStringList selectedPaths() const override { return selection; }
    QString focusedPath() const override { return selection.value(0); }
    bool promptText(const QString &, const QString &, QString &value) override
    {
        if (promptAnswers.isEmpty())
            return false;
        value = promptAnswers.takeFirst();
        return true;
    }
    bool confirm(const QString &, const QString &) override { return confirmAnswer; }
    void showError(const QString &title, const QString &text) override { errors << title + QStringLiteral(": ") + text; }
    QString chooseDestination(const QString &) override { return {}; }
    bool go(const ParsedCommand &cmd, QString *) override
    {
        QString s = cmd.value(QStringLiteral("PATH"));
        for (const QString &f : {QStringLiteral("UP"), QStringLiteral("BACK"), QStringLiteral("NEWTAB"), QStringLiteral("OPENINDEST")}) {
            if (cmd.hasFlag(f))
                s += QLatin1Char(' ') + f;
        }
        goCalls << s.trimmed();
        return true;
    }
    bool set(const ParsedCommand &cmd, QString *) override
    {
        setCalls << cmd.raw;
        return true;
    }
    bool select(const ParsedCommand &, QString *) override { return true; }
    bool beginInlineRename() override
    {
        ++inlineRenames;
        return true;
    }
    void afterLocalChange(const QStringList &paths) override { selected = paths; }
};

} // namespace

class TestCommandEngine : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        Paths::setConfigDirOverride(m_dir->filePath(QStringLiteral("cfg")));
        Paths::setDataHomeOverride(m_dir->filePath(QStringLiteral("data")));
        m_aliases = std::make_unique<AliasManager>(m_dir->filePath(QStringLiteral("aliases.ini")));
        m_jobs = std::make_unique<JobManager>();
        m_engine = std::make_unique<CommandEngine>(*m_aliases, *m_jobs);
        ctx = FakeContext();
        ctx.source = m_dir->filePath(QStringLiteral("src"));
        ctx.dest = m_dir->filePath(QStringLiteral("dst"));
        QDir().mkpath(ctx.source);
        QDir().mkpath(ctx.dest);
    }

    void goResolvesAliasesAndRelative()
    {
        QString error;
        QVERIFY(m_aliases->setUserAlias(QStringLiteral("work"), ctx.dest, &error));
        QVERIFY(m_engine->run(QStringLiteral("Go /work NEWTAB"), ctx));
        QCOMPARE(ctx.goCalls.last(), ctx.dest + QStringLiteral(" NEWTAB"));
        QDir().mkpath(ctx.source + QStringLiteral("/sub"));
        QVERIFY(m_engine->run(QStringLiteral("Go sub"), ctx));
        QCOMPARE(ctx.goCalls.last(), ctx.source + QStringLiteral("/sub"));
        QVERIFY(m_engine->run(QStringLiteral("Go UP"), ctx));
        QCOMPARE(ctx.goCalls.last(), QStringLiteral("UP"));
        QVERIFY(!m_engine->run(QStringLiteral("Go /nonexistent-xyz-123"), ctx));
        QVERIFY(!ctx.errors.isEmpty());
    }

    void pasteFromFlatViewKeepsFolders()
    {
        writeFile(ctx.source + QStringLiteral("/top.txt"), "t");
        writeFile(ctx.source + QStringLiteral("/sub/x.txt"), "x");
        ctx.selection = {ctx.source + QStringLiteral("/top.txt"), ctx.source + QStringLiteral("/sub/x.txt")};
        QVERIFY(m_engine->run(QStringLiteral("Clipboard COPY"), ctx));
        const QString from = ctx.source;
        ctx.source = ctx.dest; // paste into the other folder
        QVERIFY(m_engine->run(QStringLiteral("Clipboard PASTE STRUCTURE=keep"), ctx));
        QVERIFY(m_jobs->waitForIdle(5000));
        QVERIFY(QFile::exists(ctx.dest + QStringLiteral("/sub/x.txt")));
        QVERIFY(QFile::exists(ctx.dest + QStringLiteral("/top.txt")));
        // Asked, and canceled (the fake context has no answer): nothing pasted.
        const QString other = m_dir->filePath(QStringLiteral("other"));
        QDir().mkpath(other);
        ctx.source = other;
        QVERIFY(!m_engine->run(QStringLiteral("Clipboard PASTE"), ctx));
        QVERIFY(m_jobs->waitForIdle(5000));
        QVERIFY(QDir(other).isEmpty());
        QVERIFY(ctx.errors.isEmpty()); // canceling is not an error
        ctx.source = from;
    }

    void setAttrAndMakeLink()
    {
        writeFile(ctx.source + QStringLiteral("/a.txt"), "a");
        writeFile(ctx.source + QStringLiteral("/Game/x.d64"), "x");
        ctx.selection = {ctx.source + QStringLiteral("/a.txt")};
        QVERIFY(m_engine->run(QStringLiteral("SetAttr MODIFIED=\"2010-01-02 03:04\" PERMS=600"), ctx));
        QCOMPARE(QFileInfo(ctx.source + QStringLiteral("/a.txt")).lastModified(), QDateTime(QDate(2010, 1, 2), QTime(3, 4)));
        QCOMPARE(QFileInfo(ctx.source + QStringLiteral("/a.txt")).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions());
        QVERIFY(!m_engine->run(QStringLiteral("SetAttr PERMS=banana"), ctx));
        QVERIFY(!m_engine->run(QStringLiteral("SetAttr MODIFIED=soon"), ctx));
        QVERIFY(!m_engine->run(QStringLiteral("SetAttr"), ctx)); // the window: none here
        // Links: beside the item, in the destination, by a relative path.
        ctx.selection = {ctx.source + QStringLiteral("/a.txt"), ctx.source + QStringLiteral("/Game")};
        QVERIFY(m_engine->run(QStringLiteral("MakeLink"), ctx));
        QVERIFY(QFileInfo(ctx.source + QStringLiteral("/a (link).txt")).isSymLink());
        QVERIFY(QFileInfo(ctx.source + QStringLiteral("/Game (link)")).isSymLink());
        QVERIFY(m_engine->run(QStringLiteral("MakeLink DEST RELATIVE"), ctx));
        QVERIFY(QFileInfo(ctx.dest + QStringLiteral("/Game")).isSymLink());
        QCOMPARE(QFileInfo(ctx.dest + QStringLiteral("/Game")).symLinkTarget(), ctx.source + QStringLiteral("/Game"));
        QVERIFY(QFile::exists(ctx.dest + QStringLiteral("/Game/x.d64")));
        QVERIFY(m_engine->run(QStringLiteral("MakeLink DEST"), ctx)); // taken: new names
        QVERIFY(QFileInfo(ctx.dest + QStringLiteral("/Game (2)")).isSymLink());
        // Undo removes the newest links.
        QVERIFY(m_engine->run(QStringLiteral("Undo"), ctx));
        QVERIFY(m_jobs->waitForIdle(5000));
        QTRY_VERIFY(!QFileInfo(ctx.dest + QStringLiteral("/Game (2)")).isSymLink());
        QVERIFY(QFile::exists(ctx.source + QStringLiteral("/Game/x.d64")));
    }

    void terminalOpensInTheFolder()
    {
        QStringList ran;
        QString ranIn;
        HostRun::setStarter([&](const QStringList &cmd, const QString &dir) {
            ran = cmd;
            ranIn = dir;
            return true;
        });
        QVERIFY(m_engine->run(QStringLiteral("Terminal"), ctx));
        QCOMPARE(ranIn, ctx.source);
        QVERIFY(ran.join(QLatin1Char(' ')).contains(QStringLiteral("konsole --workdir '%1'").arg(ctx.source)));
        QVERIFY(m_engine->run(QStringLiteral("Terminal PATH=\"%1\" PROGRAM=kitty").arg(ctx.dest), ctx));
        QCOMPARE(ranIn, ctx.dest);
        // The program asked for comes first; the others are fallbacks.
        const QString script = ran.last();
        QVERIFY(script.indexOf(QStringLiteral("'kitty'")) < script.indexOf(QStringLiteral("'konsole'")));
        QVERIFY(!m_engine->run(QStringLiteral("Terminal PATH=/nonexistent-xyz"), ctx));
        HostRun::setStarter({});
        // A folder with a quote in its name stays one argument.
        const QStringList cmd = HostRun::terminalCommand(QStringLiteral("/tmp/it's here"), {});
        QVERIFY(cmd.last().contains(QStringLiteral("'/tmp/it'\\''s here'")));
    }

    void copySelectionToDest()
    {
        writeFile(ctx.source + QStringLiteral("/a.txt"), "a");
        ctx.selection = {ctx.source + QStringLiteral("/a.txt")};
        QVERIFY(m_engine->run(QStringLiteral("Copy"), ctx));
        QVERIFY(m_jobs->waitForIdle(5000));
        QVERIFY(QFile::exists(ctx.dest + QStringLiteral("/a.txt")));
        QVERIFY(QFile::exists(ctx.source + QStringLiteral("/a.txt")));
    }

    void moveWithToAndCode()
    {
        writeFile(ctx.source + QStringLiteral("/m.txt"), "m");
        QDir().mkpath(ctx.dest + QStringLiteral("/inbox"));
        ctx.selection = {ctx.source + QStringLiteral("/m.txt")};
        QVERIFY(m_engine->run(QStringLiteral("Copy MOVE TO={destpath}/inbox"), ctx));
        QVERIFY(m_jobs->waitForIdle(5000));
        QVERIFY(QFile::exists(ctx.dest + QStringLiteral("/inbox/m.txt")));
        QVERIFY(!QFile::exists(ctx.source + QStringLiteral("/m.txt")));
    }

    void copyAsPrompts()
    {
        writeFile(ctx.source + QStringLiteral("/IMG1.JPG"));
        ctx.selection = {ctx.source + QStringLiteral("/IMG1.JPG")};
        ctx.promptAnswers = {QStringLiteral("Beach1.jpg")};
        QVERIFY(m_engine->run(QStringLiteral("Copy AS"), ctx));
        QVERIFY(m_jobs->waitForIdle(5000));
        QVERIFY(QFile::exists(ctx.dest + QStringLiteral("/Beach1.jpg")));
        // Canceling the prompt stops quietly.
        ctx.promptAnswers.clear();
        ctx.errors.clear();
        QVERIFY(!m_engine->run(QStringLiteral("Copy AS"), ctx));
        QVERIFY(ctx.errors.isEmpty());
    }

    void requiredCodeStops()
    {
        ctx.selection.clear();
        QVERIFY(!m_engine->run(QStringLiteral("Copy {filepath$}\nGo UP"), ctx));
        QVERIFY(ctx.goCalls.isEmpty());
        QCOMPARE(ctx.errors.size(), 1);
    }

    void deleteTrashesAndConfirmsPermanent()
    {
        writeFile(ctx.source + QStringLiteral("/t1"));
        writeFile(ctx.source + QStringLiteral("/t2"));
        ctx.selection = {ctx.source + QStringLiteral("/t1")};
        QVERIFY(m_engine->run(QStringLiteral("Delete"), ctx));
        QVERIFY(m_jobs->waitForIdle(5000));
        QVERIFY(!QFile::exists(ctx.source + QStringLiteral("/t1")));

        ctx.selection = {ctx.source + QStringLiteral("/t2")};
        ctx.confirmAnswer = false;
        QVERIFY(!m_engine->run(QStringLiteral("Delete NORECYCLE"), ctx));
        QVERIFY(QFile::exists(ctx.source + QStringLiteral("/t2")));
        ctx.confirmAnswer = true;
        QVERIFY(m_engine->run(QStringLiteral("Delete NORECYCLE"), ctx));
        QVERIFY(m_jobs->waitForIdle(5000));
        QVERIFY(!QFile::exists(ctx.source + QStringLiteral("/t2")));
    }

    void renameInlineAndExplicit()
    {
        QVERIFY(m_engine->run(QStringLiteral("Rename"), ctx));
        QCOMPARE(ctx.inlineRenames, 1);
        writeFile(ctx.source + QStringLiteral("/House_03e12.MPG"));
        QVERIFY(m_engine->run(QStringLiteral("Rename FROM=House_03e12.MPG TO=HouseMD_3x12.MPG"), ctx));
        QCOMPARE(ctx.selected, QStringList{ctx.source + QStringLiteral("/HouseMD_3x12.MPG")});
    }

    void createFoldersCsv()
    {
        QVERIFY(m_engine->run(QStringLiteral("CreateFolder NAME=\"A,B,C\""), ctx));
        QCOMPARE(ctx.selected.size(), 3);
        ctx.promptAnswers = {QStringLiteral("Joan Jett, Beach Boys")};
        QVERIFY(m_engine->run(QStringLiteral("CreateFolder"), ctx));
        QVERIFY(QFileInfo(ctx.source + QStringLiteral("/Beach Boys")).isDir());
    }

    void clipboardCutPaste()
    {
        writeFile(ctx.source + QStringLiteral("/clip.txt"));
        ctx.selection = {ctx.source + QStringLiteral("/clip.txt")};
        QVERIFY(m_engine->run(QStringLiteral("Clipboard CUT"), ctx));
        FakeContext other = ctx;
        other.source = ctx.dest;
        QVERIFY(m_engine->run(QStringLiteral("Clipboard PASTE"), other));
        QVERIFY(m_jobs->waitForIdle(5000));
        QVERIFY(QFile::exists(ctx.dest + QStringLiteral("/clip.txt")));
        QVERIFY(!QFile::exists(ctx.source + QStringLiteral("/clip.txt")));
    }

    void unknownCommand()
    {
        QVERIFY(!m_engine->run(QStringLiteral("Frobnicate"), ctx));
        QVERIFY(ctx.errors.last().contains(QStringLiteral("Unknown command")));
    }

    void setAndSelectAreDelegated()
    {
        QVERIFY(m_engine->run(QStringLiteral("Set DUAL=toggle"), ctx));
        QCOMPARE(ctx.setCalls, QStringList{QStringLiteral("Set DUAL=toggle")});
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<AliasManager> m_aliases;
    std::unique_ptr<JobManager> m_jobs;
    std::unique_ptr<CommandEngine> m_engine;
    FakeContext ctx;
};

QTEST_MAIN(TestCommandEngine)
#include "tst_commandengine.moc"
