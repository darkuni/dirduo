#include "core/AliasManager.h"
#include "core/CommandEngine.h"
#include "core/HostRun.h"
#include "core/ScriptRunner.h"
#include "core/Servers.h"
#include "core/JobManager.h"
#include "core/Paths.h"
#include "core/FileOps.h"

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

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
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

    void serverAddressBook()
    {
        QCOMPARE(Servers::makeUrl(QStringLiteral("sftp"), QStringLiteral("nas"), 0, QStringLiteral("deck"), QStringLiteral("media")),
                 QStringLiteral("sftp://deck@nas/media"));
        QCOMPARE(Servers::makeUrl(QStringLiteral("smb"), QStringLiteral("box"), 445, {}, QStringLiteral("/Games")),
                 QStringLiteral("smb://box:445/Games"));
        QVERIFY(Servers::isServerUrl(QStringLiteral("smb://box/share")));
        QVERIFY(!Servers::isServerUrl(QStringLiteral("/home/deck")));
        QVERIFY(!Servers::isServerUrl(QStringLiteral("http://example.com")));
        QCOMPARE(Servers::suggestedName(QStringLiteral("sftp://deck@nas/media")), QStringLiteral("nas media (SFTP)"));

        QVERIFY(m_engine->run(QStringLiteral("Server ADD URL=sftp://deck@nas/media NAME=NAS"), ctx));
        QVERIFY(m_engine->run(QStringLiteral("Server ADD URL=smb://box/Games"), ctx));
        QCOMPARE(Servers::instance()->items().size(), 2);
        QCOMPARE(Servers::instance()->items().value(1).name, QStringLiteral("box Games (Windows share)"));
        // Same name: replaced, not doubled.
        QVERIFY(m_engine->run(QStringLiteral("Server ADD URL=sftp://deck@nas2/media NAME=nas"), ctx));
        QCOMPARE(Servers::instance()->items().size(), 2);
        QCOMPARE(Servers::instance()->urlFor(QStringLiteral("NAS")), QStringLiteral("sftp://deck@nas2/media"));
        QVERIFY(!m_engine->run(QStringLiteral("Server ADD URL=/home/deck NAME=x"), ctx));
        // GO opens it like Go would.
        QVERIFY(m_engine->run(QStringLiteral("Server GO=NAS NEWTAB"), ctx));
        QCOMPARE(ctx.goCalls.last(), QStringLiteral("sftp://deck@nas2/media NEWTAB"));
        QVERIFY(!m_engine->run(QStringLiteral("Server GO=nowhere"), ctx));
        QVERIFY(m_engine->run(QStringLiteral("Server REMOVE=NAS"), ctx));
        QCOMPARE(Servers::instance()->items().size(), 1);
        QVERIFY(!m_engine->run(QStringLiteral("Server REMOVE=NAS"), ctx));
        Servers::instance()->setItems({});
    }

    void buttonScripts()
    {
        Paths::setCacheHomeOverride(m_dir->filePath(QStringLiteral("cache")));
        writeFile(ctx.source + QStringLiteral("/a b.txt"), "a");
        writeFile(ctx.source + QStringLiteral("/c.txt"), "c");
        ctx.selection = {ctx.source + QStringLiteral("/a b.txt"), ctx.source + QStringLiteral("/c.txt")};
        const QString script = QStringLiteral("#!/bin/bash\n# linopus: output\nout=\"$LINOPUS_DEST/out.txt\"\n"
                                              "pwd > \"$out\"\necho \"$LINOPUS_SOURCE\" >> \"$out\"\n"
                                              "for f in \"$@\"; do basename \"$f\" >> \"$out\"; done\necho done\n");
        QVERIFY(ScriptRunner::isScript(script));
        QVERIFY(!ScriptRunner::isScript(QStringLiteral("Copy")));
        QVERIFY(ScriptRunner::optionsOf(script).showOutput);
        QVERIFY(!ScriptRunner::optionsOf(script).terminal);
        QVERIFY(ScriptRunner::optionsOf(QStringLiteral("#!/bin/sh\n# linopus: terminal\n# Linopus: NoRefresh")).terminal);
        QVERIFY(!ScriptRunner::optionsOf(QStringLiteral("#!/bin/sh\n#linopus:norefresh")).refresh);

        QVERIFY(m_engine->run(script, ctx));
        QTRY_VERIFY(m_engine->runningScripts().isEmpty() || m_engine->runningScripts().first()->waitForFinished(10000));
        QTRY_VERIFY(QFile::exists(ctx.dest + QStringLiteral("/out.txt")));
        QTRY_COMPARE(readFile(ctx.dest + QStringLiteral("/out.txt")).split('\n').size(), 5);
        const QList<QByteArray> lines = readFile(ctx.dest + QStringLiteral("/out.txt")).split('\n');
        QCOMPARE(QString::fromUtf8(lines.value(0)), ctx.source); // starts in the source folder
        QCOMPARE(QString::fromUtf8(lines.value(1)), ctx.source);
        QCOMPARE(lines.value(2), QByteArray("a b.txt")); // spaces survive
        QCOMPARE(lines.value(3), QByteArray("c.txt"));
        // The script file is cleaned up afterwards.
        QTRY_VERIFY(QDir(ScriptRunner::scriptDir()).entryList(QDir::Files).isEmpty());
        Paths::setCacheHomeOverride(QString());
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

    void pieceSizes()
    {
        qint64 b = 0;
        QVERIFY(CommandEngine::parsePieceSize(QStringLiteral("100M"), &b));
        QCOMPARE(b, Q_INT64_C(100) * 1024 * 1024);
        QVERIFY(CommandEngine::parsePieceSize(QStringLiteral("1.5 GB"), &b));
        QCOMPARE(b, Q_INT64_C(1610612736));
        QVERIFY(CommandEngine::parsePieceSize(QStringLiteral("fat32"), &b));
        QCOMPARE(b, Q_INT64_C(4294967295));
        QVERIFY(CommandEngine::parsePieceSize(QStringLiteral("4096"), &b));
        QCOMPARE(b, Q_INT64_C(4096));
        QVERIFY(!CommandEngine::parsePieceSize(QStringLiteral("big"), &b));
        QVERIFY(!CommandEngine::parsePieceSize(QStringLiteral("-3M"), &b));
    }

    void splitAndJoin()
    {
        QByteArray data(10000, Qt::Uninitialized);
        for (int i = 0; i < data.size(); ++i)
            data[i] = char(i * 7);
        writeFile(ctx.source + QStringLiteral("/game.iso"), data);
        ctx.selection = {ctx.source + QStringLiteral("/game.iso")};
        QVERIFY(m_engine->run(QStringLiteral("Split SIZE=4K"), ctx));
        QVERIFY(m_jobs->waitForIdle(5000));
        QCOMPARE(QFileInfo(ctx.source + QStringLiteral("/game.iso.001")).size(), 4096);
        QCOMPARE(QFileInfo(ctx.source + QStringLiteral("/game.iso.002")).size(), 4096);
        QCOMPARE(QFileInfo(ctx.source + QStringLiteral("/game.iso.003")).size(), 10000 - 8192);
        QVERIFY(!QFile::exists(ctx.source + QStringLiteral("/game.iso.004")));
        // Already there: not touched unless told to replace.
        QVERIFY(m_engine->run(QStringLiteral("Split SIZE=4K"), ctx));
        QVERIFY(m_jobs->waitForIdle(5000));
        QCOMPARE(QFileInfo(ctx.source + QStringLiteral("/game.iso.003")).size(), 10000 - 8192);
        // PIECES= splits evenly, into the destination.
        QVERIFY(m_engine->run(QStringLiteral("Split PIECES=2 TO=dest"), ctx));
        QVERIFY(m_jobs->waitForIdle(5000));
        QCOMPARE(QFileInfo(ctx.dest + QStringLiteral("/game.iso.001")).size(), 5000);
        QCOMPARE(QFileInfo(ctx.dest + QStringLiteral("/game.iso.002")).size(), 5000);
        // Join from the first piece alone, into the destination.
        QCOMPARE(CommandEngine::piecesFrom({ctx.source + QStringLiteral("/game.iso.001")}).size(), 3);
        QFile::remove(ctx.dest + QStringLiteral("/game.iso.001"));
        QFile::remove(ctx.dest + QStringLiteral("/game.iso.002"));
        ctx.selection = {ctx.source + QStringLiteral("/game.iso.001")};
        QVERIFY(m_engine->run(QStringLiteral("Join TO=dest"), ctx));
        QVERIFY(m_jobs->waitForIdle(5000));
        QCOMPARE(readFile(ctx.dest + QStringLiteral("/game.iso")), data);
        // Undo takes the joined file away.
        QVERIFY(m_engine->run(QStringLiteral("Undo"), ctx));
        QVERIFY(m_jobs->waitForIdle(5000));
        QVERIFY(!QFile::exists(ctx.dest + QStringLiteral("/game.iso")));
        ctx.selection = {ctx.source + QStringLiteral("/game.iso")};
        QVERIFY(!m_engine->run(QStringLiteral("Join"), ctx)); // one file is no set
        QVERIFY(!m_engine->run(QStringLiteral("Split SIZE=huge"), ctx));
    }

    void cleanJunk()
    {
        QVERIFY(FileOps::isJunk(QStringLiteral(".DS_Store"), false));
        QVERIFY(FileOps::isJunk(QStringLiteral("._Turrican.d64"), false));
        QVERIFY(FileOps::isJunk(QStringLiteral("Thumbs.db"), false));
        QVERIFY(FileOps::isJunk(QStringLiteral("__MACOSX"), true));
        QVERIFY(!FileOps::isJunk(QStringLiteral("__MACOSX"), false));
        QVERIFY(!FileOps::isJunk(QStringLiteral("._"), false));
        QVERIFY(!FileOps::isJunk(QStringLiteral("readme.txt"), false));
        QDir().mkpath(ctx.source + QStringLiteral("/roms/__MACOSX/roms"));
        writeFile(ctx.source + QStringLiteral("/roms/__MACOSX/roms/._a.d64"));
        writeFile(ctx.source + QStringLiteral("/roms/a.d64"));
        writeFile(ctx.source + QStringLiteral("/roms/._a.d64"));
        writeFile(ctx.source + QStringLiteral("/roms/.DS_Store"));
        writeFile(ctx.source + QStringLiteral("/roms/Desktop.ini"));
        const QStringList junk = FileOps::findJunk({ctx.source + QStringLiteral("/roms")});
        QCOMPARE(junk.size(), 4); // __MACOSX whole, not what is in it
        ctx.selection = {ctx.source + QStringLiteral("/roms")};
        ctx.confirmAnswer = false;
        QVERIFY(!m_engine->run(QStringLiteral("Clean"), ctx));
        QVERIFY(QFile::exists(ctx.source + QStringLiteral("/roms/.DS_Store")));
        ctx.confirmAnswer = true;
        QVERIFY(m_engine->run(QStringLiteral("Clean"), ctx));
        QVERIFY(m_jobs->waitForIdle(5000));
        QCOMPARE(QDir(ctx.source + QStringLiteral("/roms")).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
                 QStringList{QStringLiteral("a.d64")});
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
