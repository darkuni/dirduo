#include "core/AliasManager.h"
#include "core/CommandCatalog.h"
#include "core/CommandEngine.h"
#include "core/JobManager.h"
#include "core/Paths.h"
#include "core/Scripting.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}

class FakeContext : public CommandContext
{
public:
    QString source;
    QStringList selection;
    QStringList promptAnswers;
    QStringList messages;
    QStringList errors;
    QStringList goCalls;
    QStringList selected;
    int choice = 0;

    QString sourcePath() const override { return source; }
    QString destPath() const override { return {}; }
    QStringList selectedPaths() const override { return selection; }
    QString focusedPath() const override { return selection.value(0); }
    bool promptText(const QString &, const QString &, QString &value) override
    {
        if (promptAnswers.isEmpty())
            return false;
        value = promptAnswers.takeFirst();
        return true;
    }
    bool confirm(const QString &, const QString &) override { return true; }
    void showError(const QString &title, const QString &text) override { errors << title + QStringLiteral(": ") + text; }
    QString chooseDestination(const QString &) override { return {}; }
    int choose(const QString &, const QString &text, const QStringList &) override
    {
        messages << text;
        return choice;
    }
    bool go(const ParsedCommand &cmd, QString *) override
    {
        goCalls << cmd.positional.value(0);
        return true;
    }
    bool set(const ParsedCommand &, QString *) override { return true; }
    bool select(const ParsedCommand &, QString *) override { return true; }
    bool beginInlineRename() override { return true; }
    void afterLocalChange(const QStringList &paths) override { selected = paths; }
};

} // namespace

class TestScripting : public QObject
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
        m_host = std::make_unique<ScriptHost>();
        m_host->setFolder(m_dir->filePath(QStringLiteral("scripts")));
        m_host->setEngine(m_engine.get());
        m_engine->setScripts(m_host.get());
        ctx = FakeContext();
        ctx.source = m_dir->filePath(QStringLiteral("src"));
        QDir().mkpath(ctx.source);
    }
    void cleanup()
    {
        m_engine->setScripts(nullptr);
        m_host.reset();
        CommandCatalog::setScriptCommands({});
    }

    void commandsRunWithTheLister()
    {
        writeFile(m_host->folder() + QStringLiteral("/tools.js"), R"JS(
dirduo.info({name: "Tools", version: "2.0", description: "Test tools"});
dirduo.command({name: "CountThem", summary: "Counts", args: ["TIMES="], run(ctx) {
    const n = ctx.selected.length * Number(ctx.args.TIMES || 1);
    ctx.message("count " + n + (ctx.has("LOUD") ? "!" : "") + " in " + dirduo.nameOf(ctx.source));
    dirduo.save("last", n);
}});
dirduo.command({name: "Fails", run(ctx) { throw new Error("broken on purpose"); }});
dirduo.command({name: "SaysNo", run(ctx) { return false; }});
dirduo.command({name: "Copy", run(ctx) {}});   // a built-in name: refused
)JS");
        m_host->reload();
        const QList<ScriptInfo> infos = m_host->scripts();
        QCOMPARE(infos.size(), 1);
        QCOMPARE(infos.first().name, QStringLiteral("Tools"));
        QCOMPARE(infos.first().version, QStringLiteral("2.0"));
        QCOMPARE(infos.first().commands, (QStringList{QStringLiteral("CountThem"), QStringLiteral("Fails"), QStringLiteral("SaysNo")}));
        QVERIFY(CommandCatalog::find(QStringLiteral("countthem")));
        QVERIFY(CommandCatalog::complete(QStringLiteral("Coun")).contains(QStringLiteral("CountThem")));
        QVERIFY(m_host->log().join(QLatin1Char('\n')).contains(QStringLiteral("Copy is a built-in command")));

        ctx.selection = {ctx.source + QStringLiteral("/a"), ctx.source + QStringLiteral("/b")};
        QVERIFY(m_engine->run(QStringLiteral("CountThem TIMES=3 LOUD"), ctx));
        QCOMPARE(ctx.messages.last(), QStringLiteral("count 6! in src"));
        // Errors from the script reach the lister with their line.
        QVERIFY(!m_engine->run(QStringLiteral("Fails"), ctx));
        QVERIFY(ctx.errors.last().contains(QStringLiteral("broken on purpose")));
        QVERIFY(ctx.errors.last().contains(QStringLiteral("Line")));
        QVERIFY(!m_engine->run(QStringLiteral("SaysNo\nCountThem"), ctx)); // stops the button there
        // Saved values come back after a reload.
        m_host->reload();
        writeFile(m_host->folder() + QStringLiteral("/reader.js"), R"JS(
dirduo.command({name: "ShowLast", run(ctx) { ctx.message("was " + dirduo.load("last", -1)); }});
)JS");
        m_host->reload();
        QVERIFY(m_engine->run(QStringLiteral("ShowLast"), ctx));
        QCOMPARE(ctx.messages.last(), QStringLiteral("was -1")); // each script has its own
    }

    void filesAndRunningCommands()
    {
        writeFile(ctx.source + QStringLiteral("/one.txt"), "hello world");
        writeFile(m_host->folder() + QStringLiteral("/files.js"), R"JS(
dirduo.command({name: "FileWork", run(ctx) {
    const items = dirduo.list(ctx.source);
    const text = dirduo.readText(items[0].path);
    const bytes = dirduo.readBytes(items[0].path, 0, 5);
    const out = dirduo.join(ctx.source, "out.txt");
    dirduo.writeText(out, text.toUpperCase() + " " + bytes.length + " " + items[0].ext + " " + (items[0].modified instanceof Date));
    ctx.select([out]);
    ctx.run('Go "' + ctx.source + '"');
}});
)JS");
        m_host->reload();
        QVERIFY(m_engine->run(QStringLiteral("FileWork"), ctx));
        QFile f(ctx.source + QStringLiteral("/out.txt"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("HELLO WORLD 5 txt true"));
        QCOMPARE(ctx.selected, QStringList{ctx.source + QStringLiteral("/out.txt")});
        QCOMPARE(ctx.goCalls.last(), ctx.source);
    }

    void columnsAreWorkedOutInTheBackground()
    {
        writeFile(m_host->folder() + QStringLiteral("/cols.js"), R"JS(
dirduo.column({name: "Region", value(item) { const m = item.name.match(/\((USA|Japan)\)/); return m ? m[1] : ""; }});
dirduo.column({name: "Len", label: "Name Length", numeric: true, value(item) { return item.name.length; }});
dirduo.column({name: "Bad", value(item) { return nope.nothing; }});
)JS");
        m_host->reload();
        QCOMPARE(m_host->columns().size(), 3);
        QCOMPARE(m_host->columnForSlot(1).label, QStringLiteral("Name Length"));
        FileItem it;
        it.path = ctx.source + QStringLiteral("/Contra (USA).nes");
        it.name = QStringLiteral("Contra (USA).nes");
        it.size = 10;
        QSignalSpy ready(m_host.get(), &ScriptHost::columnValueReady);
        QVariant v;
        QVERIFY(!m_host->columnValue(0, it, &v)); // queued
        QVERIFY(!m_host->columnValue(1, it, &v));
        QVERIFY(!m_host->columnValue(2, it, &v));
        QTRY_VERIFY(m_host->columnValue(0, it, &v));
        QCOMPARE(v.toString(), QStringLiteral("USA"));
        QVERIFY(m_host->columnValue(1, it, &v));
        QCOMPARE(v.toDouble(), 16.0);
        QVERIFY(m_host->columnValue(2, it, &v)); // its error leaves it empty
        QVERIFY(!v.isValid());
        QVERIFY(m_host->log().join(QLatin1Char('\n')).contains(QStringLiteral("nope")));
        QVERIFY(!ready.isEmpty());
        // A changed file is worked out again.
        it.size = 11;
        QVERIFY(!m_host->columnValue(0, it, &v));
    }

    void eventsAndRunaways()
    {
        m_host->setTimeouts(300, 100);
        writeFile(m_host->folder() + QStringLiteral("/events.js"), R"JS(
dirduo.on("folderOpened", (e, ctx) => { dirduo.save("seen", e.path); ctx.message("opened " + e.path); });
dirduo.on("nothingLikeThis", e => {});
dirduo.command({name: "Forever", run(ctx) { for (;;) {} }});
)JS");
        writeFile(m_host->folder() + QStringLiteral("/broken.js"), "this is not javascript (");
        m_host->reload();
        QVariantMap data;
        data.insert(QStringLiteral("path"), QStringLiteral("/roms"));
        m_host->fire(QStringLiteral("folderOpened"), data, &ctx);
        QCOMPARE(ctx.messages.last(), QStringLiteral("opened /roms"));
        QVERIFY(m_host->log().join(QLatin1Char('\n')).contains(QStringLiteral("no event \"nothingLikeThis\"")));
        // A script stuck in a loop is stopped, and the app carries on.
        QElapsedTimer t;
        t.start();
        QVERIFY(!m_engine->run(QStringLiteral("Forever"), ctx));
        QVERIFY(t.elapsed() < 5000);
        QVERIFY(ctx.errors.last().contains(QStringLiteral("stopped")));
        // One script that does not parse: listed with its error, the others still work.
        bool sawBroken = false;
        for (const ScriptInfo &i : m_host->scripts()) {
            if (i.id == QLatin1String("broken")) {
                sawBroken = true;
                QVERIFY(!i.error.isEmpty());
            }
        }
        QVERIFY(sawBroken);
        // Turned off, its commands go.
        m_host->setEnabled(QStringLiteral("events"), false);
        QVERIFY(!m_host->handles(QStringLiteral("Forever")));
        m_host->setEnabled(QStringLiteral("events"), true);
        QVERIFY(m_host->handles(QStringLiteral("Forever")));
    }

    void newScriptsAndExamples()
    {
        QString error;
        const QString path = m_host->newScript(QStringLiteral("ROM tools"), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        QVERIFY(path.endsWith(QStringLiteral("/rom-tools.js")));
        QVERIFY(m_host->handles(QStringLiteral("RomTools"))); // the template works as it is
        QVERIFY(m_host->newScript(QStringLiteral("ROM tools"), &error).isEmpty()); // already there
        QCOMPARE(m_host->addExamples().size(), 3);
        QVERIFY(m_engine->run(QStringLiteral("Script RELOAD"), ctx));
        for (const ScriptInfo &i : m_host->scripts())
            QVERIFY2(i.error.isEmpty(), qPrintable(i.id + QStringLiteral(": ") + i.error));
        QVERIFY(m_host->handles(QStringLiteral("SelectRegion")));
        QVERIFY(m_host->handles(QStringLiteral("MakePlaylist")));
        QVERIFY(m_host->handles(QStringLiteral("RecentFolders")));
        // The examples do their jobs.
        writeFile(ctx.source + QStringLiteral("/Contra (USA).nes"), "x");
        writeFile(ctx.source + QStringLiteral("/Contra (Japan).nes"), "x");
        QVERIFY(m_engine->run(QStringLiteral("SelectRegion REGION=Japan"), ctx));
        QCOMPARE(ctx.selected, QStringList{ctx.source + QStringLiteral("/Contra (Japan).nes")});
        writeFile(ctx.source + QStringLiteral("/a.mp3"), "x");
        writeFile(ctx.source + QStringLiteral("/b.flac"), "x");
        QVERIFY(m_engine->run(QStringLiteral("MakePlaylist NAME=Mix"), ctx));
        QFile m3u(ctx.source + QStringLiteral("/Mix.m3u"));
        QVERIFY(m3u.open(QIODevice::ReadOnly));
        QCOMPARE(m3u.readAll(), QByteArray("#EXTM3U\na.mp3\nb.flac\n"));
        QVariantMap opened;
        opened.insert(QStringLiteral("path"), ctx.source);
        m_host->fire(QStringLiteral("folderOpened"), opened, &ctx);
        ctx.choice = 0;
        QVERIFY(m_engine->run(QStringLiteral("RecentFolders"), ctx));
        QCOMPARE(ctx.goCalls.last(), ctx.source);
        QVERIFY(m_engine->run(QStringLiteral("Script FOLDER"), ctx));
        QCOMPARE(ctx.goCalls.last(), m_host->folder());
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<AliasManager> m_aliases;
    std::unique_ptr<JobManager> m_jobs;
    std::unique_ptr<CommandEngine> m_engine;
    std::unique_ptr<ScriptHost> m_host;
    FakeContext ctx;
};

QTEST_GUILESS_MAIN(TestScripting)
#include "tst_scripting.moc"
