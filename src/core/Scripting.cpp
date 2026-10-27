#include "core/Scripting.h"

#include "core/CommandCatalog.h"
#include "core/CommandEngine.h"
#include "core/HostRun.h"
#include "core/Location.h"
#include "core/Paths.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QTime>
#include <QTimer>

#include <condition_variable>
#include <mutex>
#include <thread>

namespace {
ScriptHost *g_host = nullptr;
int g_runDepth = 0; // ctx.run inside a script command, inside another...

const QStringList kEvents = {QStringLiteral("folderOpened"), QStringLiteral("jobFinished"), QStringLiteral("started")};

QJSValue itemValue(QJSEngine *e, const QString &path, const QString &name, qint64 size, const QDateTime &modified, bool isDir)
{
    QJSValue o = e->newObject();
    o.setProperty(QStringLiteral("path"), path);
    o.setProperty(QStringLiteral("name"), name);
    o.setProperty(QStringLiteral("ext"), isDir ? QString() : QFileInfo(name).suffix());
    o.setProperty(QStringLiteral("folder"), Location::parent(path));
    o.setProperty(QStringLiteral("size"), double(isDir ? 0 : size));
    o.setProperty(QStringLiteral("modified"), e->toScriptValue(modified));
    o.setProperty(QStringLiteral("isFolder"), isDir);
    return o;
}

QJSValue itemFor(QJSEngine *e, const QString &path)
{
    const QFileInfo fi(path);
    return itemValue(e, path, fi.fileName(), fi.size(), fi.lastModified(), fi.isDir());
}

QJSValue itemFrom(QJSEngine *e, const FileItem &it)
{
    return itemValue(e, it.path, it.name, it.size, it.modified, it.isDir);
}

} // namespace

// ---------------------------------------------------------------- watchdog

// Stops a script that runs too long: QJSEngine::setInterrupted may be called
// from another thread.
class ScriptHost::Watchdog
{
public:
    Watchdog()
        : m_thread([this] { loop(); })
    {
    }
    ~Watchdog()
    {
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_quit = true;
        }
        m_cv.notify_all();
        m_thread.join();
    }
    void arm(QJSEngine *engine, int ms)
    {
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_engine = engine;
            m_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
            m_armed = true;
        }
        m_cv.notify_all();
    }
    void disarm()
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_armed = false;
        m_engine = nullptr;
    }

private:
    void loop()
    {
        std::unique_lock<std::mutex> l(m_mutex);
        while (!m_quit) {
            if (!m_armed) {
                m_cv.wait(l);
                continue;
            }
            if (m_cv.wait_until(l, m_deadline) == std::cv_status::timeout && m_armed && m_engine
                && std::chrono::steady_clock::now() >= m_deadline) {
                m_engine->setInterrupted(true);
                m_armed = false;
            }
        }
    }
    std::mutex m_mutex;
    std::condition_variable m_cv;
    QJSEngine *m_engine = nullptr;
    std::chrono::steady_clock::time_point m_deadline;
    bool m_armed = false;
    bool m_quit = false;
    std::thread m_thread;
};

// ---------------------------------------------------------------- what scripts see

class ScriptApi;
class ScriptCtx;

struct ScriptHost::Script
{
    ScriptInfo info;
    std::unique_ptr<QJSEngine> engine;
    ScriptApi *api = nullptr;  // owned by the engine's parent chain (deleted with it)
    ScriptCtx *ctx = nullptr;
    struct Command
    {
        QString name;
        QString summary;
        QStringList args;
        QJSValue run;
    };
    QHash<QString, Command> commands; // lower-case name
    QHash<QString, QJSValue> columns; // column name
    QHash<QString, QList<QJSValue>> events;
    QVariantMap vars;
    QString varsFile;
};

// The `dirduo` object.
class ScriptApi : public QObject
{
    Q_OBJECT
public:
    ScriptApi(ScriptHost *host, ScriptHost::Script *s)
        : m_host(host)
        , m_s(s)
    {
    }

    Q_INVOKABLE void info(const QJSValue &o)
    {
        auto str = [&o](const char *k) { return o.property(QLatin1String(k)).isUndefined() ? QString() : o.property(QLatin1String(k)).toString(); };
        if (!str("name").isEmpty())
            m_s->info.name = str("name");
        m_s->info.version = str("version");
        m_s->info.description = str("description");
        m_s->info.author = str("author");
    }
    Q_INVOKABLE void command(const QJSValue &def) { m_host->registerCommand(m_s, def); }
    Q_INVOKABLE void column(const QJSValue &def) { m_host->registerColumn(m_s, def); }
    Q_INVOKABLE void on(const QString &event, const QJSValue &fn) { m_host->registerEvent(m_s, event, fn); }
    Q_INVOKABLE void log(const QString &text) { m_host->addLog(m_s->info.id, text); }

    // Saved values, kept for this script between runs.
    Q_INVOKABLE QJSValue load(const QString &key, const QJSValue &fallback = QJSValue())
    {
        if (!m_s->vars.contains(key))
            return fallback;
        return m_s->engine->toScriptValue(m_s->vars.value(key));
    }
    Q_INVOKABLE void save(const QString &key, const QJSValue &value)
    {
        if (value.isUndefined() || value.isNull())
            m_s->vars.remove(key);
        else
            m_s->vars.insert(key, value.toVariant(QJSValue::ConvertJSObjects));
        QDir().mkpath(QFileInfo(m_s->varsFile).absolutePath());
        QSaveFile f(m_s->varsFile);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(QJsonObject::fromVariantMap(m_s->vars)).toJson());
            f.commit();
        }
    }

    // Files.
    Q_INVOKABLE QJSValue readText(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly) || f.size() > 16 * 1024 * 1024)
            return QJSValue(QJSValue::NullValue);
        return QJSValue(QString::fromUtf8(f.readAll()));
    }
    Q_INVOKABLE QJSValue readBytes(const QString &path, double offset = 0, double length = 4096)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly) || !f.seek(qint64(offset)))
            return QJSValue(QJSValue::NullValue);
        const QByteArray b = f.read(qBound(qint64(0), qint64(length), qint64(1024 * 1024)));
        QJSValue arr = m_s->engine->newArray(uint(b.size()));
        for (int i = 0; i < b.size(); ++i)
            arr.setProperty(quint32(i), int(quint8(b.at(i))));
        return arr;
    }
    Q_INVOKABLE bool writeText(const QString &path, const QString &text)
    {
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(text.toUtf8());
        return f.commit();
    }
    Q_INVOKABLE bool exists(const QString &path) { return QFileInfo::exists(path); }
    Q_INVOKABLE bool isFolder(const QString &path) { return QFileInfo(path).isDir(); }
    Q_INVOKABLE bool makeFolder(const QString &path) { return QDir().mkpath(path); }
    Q_INVOKABLE QJSValue stat(const QString &path)
    {
        if (!QFileInfo::exists(path))
            return QJSValue(QJSValue::NullValue);
        return itemFor(m_s->engine.get(), path);
    }
    Q_INVOKABLE QJSValue list(const QString &dir)
    {
        const QFileInfoList entries = QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
        QJSValue arr = m_s->engine->newArray(uint(entries.size()));
        for (int i = 0; i < entries.size(); ++i) {
            const QFileInfo &fi = entries.at(i);
            arr.setProperty(quint32(i), itemValue(m_s->engine.get(), fi.absoluteFilePath(), fi.fileName(), fi.size(), fi.lastModified(), fi.isDir()));
        }
        return arr;
    }
    Q_INVOKABLE QString join(const QString &dir, const QString &name) { return Location::join(dir, name); }
    Q_INVOKABLE QString parentOf(const QString &path) { return Location::parent(path); }
    Q_INVOKABLE QString nameOf(const QString &path) { return Location::fileName(path); }

    // A program on the system (outside the sandbox), waited for.
    Q_INVOKABLE QJSValue system(const QString &program, const QStringList &args = {}, double timeoutMs = 10000)
    {
        QProcess p;
        const QStringList line = HostRun::commandLine(program, args, QDir::homePath());
        p.start(line.first(), line.mid(1));
        QJSValue r = m_s->engine->newObject();
        if (!p.waitForStarted(5000)) {
            r.setProperty(QStringLiteral("code"), -1);
            r.setProperty(QStringLiteral("out"), QString());
            r.setProperty(QStringLiteral("err"), QStringLiteral("%1 could not be started.").arg(program));
            return r;
        }
        if (!p.waitForFinished(int(qBound(100.0, timeoutMs, 60000.0)))) {
            p.kill();
            p.waitForFinished(1000);
        }
        r.setProperty(QStringLiteral("code"), p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1);
        r.setProperty(QStringLiteral("out"), QString::fromUtf8(p.readAllStandardOutput()));
        r.setProperty(QStringLiteral("err"), QString::fromUtf8(p.readAllStandardError()));
        return r;
    }

private:
    ScriptHost *m_host;
    ScriptHost::Script *m_s;
};

// The `ctx` object a command or an event gets: the lister it runs in. One
// per script, pointed at the current lister while a call runs; kept by a
// script after that, it does nothing.
class ScriptCtx : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString source READ source)
    Q_PROPERTY(QString dest READ dest)
    Q_PROPERTY(QJSValue selected READ selected)
    Q_PROPERTY(QJSValue focused READ focused)
    Q_PROPERTY(QJSValue args READ args)
    Q_PROPERTY(QJSValue words READ words)
public:
    ScriptCtx(ScriptHost::Script *s, CommandEngine *engine)
        : m_s(s)
        , m_engine(engine)
    {
    }
    void bind(CommandContext *ctx, const ParsedCommand &cmd)
    {
        m_ctx = ctx;
        m_cmd = cmd;
    }
    void unbind() { m_ctx = nullptr; }

    QString source() const { return m_ctx ? m_ctx->sourcePath() : QString(); }
    QString dest() const { return m_ctx ? m_ctx->destPath() : QString(); }
    QJSValue selected() const
    {
        const QStringList paths = m_ctx ? m_ctx->selectedPaths() : QStringList();
        QJSValue arr = m_s->engine->newArray(uint(paths.size()));
        for (int i = 0; i < paths.size(); ++i)
            arr.setProperty(quint32(i), itemFor(m_s->engine.get(), paths.at(i)));
        return arr;
    }
    QJSValue focused() const
    {
        const QString f = m_ctx ? m_ctx->focusedPath() : QString();
        return f.isEmpty() ? QJSValue(QJSValue::NullValue) : itemFor(m_s->engine.get(), f);
    }
    QJSValue args() const
    {
        QJSValue o = m_s->engine->newObject();
        for (auto it = m_cmd.args.cbegin(); it != m_cmd.args.cend(); ++it)
            o.setProperty(it.key(), it.value().size() == 1 ? QJSValue(it.value().first()) : m_s->engine->toScriptValue(it.value()));
        return o;
    }
    QJSValue words() const { return m_s->engine->toScriptValue(m_cmd.positional); }

    Q_INVOKABLE bool has(const QString &word) const { return m_cmd.hasFlag(word); }
    Q_INVOKABLE bool run(const QString &commands)
    {
        if (!m_ctx || !m_engine || g_runDepth > 6)
            return false;
        ++g_runDepth;
        const bool ok = m_engine->run(commands, *m_ctx);
        --g_runDepth;
        return ok;
    }
    Q_INVOKABLE void message(const QString &text)
    {
        if (m_ctx)
            m_ctx->choose(m_s->info.name, text, {QStringLiteral("OK")});
    }
    Q_INVOKABLE QJSValue ask(const QString &label, const QString &initial = QString())
    {
        QString value = initial;
        if (!m_ctx || !m_ctx->promptText(m_s->info.name, label, value))
            return QJSValue(QJSValue::NullValue);
        return QJSValue(value);
    }
    Q_INVOKABLE bool confirm(const QString &text) { return m_ctx && m_ctx->confirm(m_s->info.name, text); }
    Q_INVOKABLE int choose(const QString &text, const QStringList &choices)
    {
        return m_ctx ? m_ctx->choose(m_s->info.name, text, choices) : -1;
    }
    // Reads the lists again and selects these items.
    Q_INVOKABLE void select(const QStringList &paths)
    {
        if (m_ctx)
            m_ctx->afterLocalChange(paths);
    }

private:
    ScriptHost::Script *m_s;
    QPointer<CommandEngine> m_engine;
    CommandContext *m_ctx = nullptr;
    ParsedCommand m_cmd;
};

// ---------------------------------------------------------------- host

ScriptHost::ScriptHost(QObject *parent)
    : QObject(parent)
    , m_queueTimer(new QTimer(this))
    , m_reloadTimer(new QTimer(this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_watchdog(std::make_unique<Watchdog>())
{
    g_host = this;
    m_folder = Paths::configDir() + QStringLiteral("/scripts");
    m_queueTimer->setSingleShot(true);
    m_queueTimer->setInterval(0);
    connect(m_queueTimer, &QTimer::timeout, this, &ScriptHost::processColumnQueue);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(400);
    connect(m_reloadTimer, &QTimer::timeout, this, &ScriptHost::reload);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &ScriptHost::scheduleReload);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &ScriptHost::scheduleReload);
    QSettings s(Paths::configFile(QStringLiteral("dirduo.ini")), QSettings::IniFormat);
    m_disabled = s.value(QStringLiteral("Scripts/disabled")).toStringList();
}

ScriptHost::~ScriptHost()
{
    unloadAll();
    if (g_host == this)
        g_host = nullptr;
}

ScriptHost *ScriptHost::instance()
{
    return g_host;
}

QString ScriptHost::folder() const
{
    return m_folder;
}

void ScriptHost::setFolder(const QString &dir)
{
    m_folder = dir;
}

void ScriptHost::setTimeouts(int commandMs, int columnMs)
{
    m_commandMs = commandMs;
    m_columnMs = columnMs;
}

void ScriptHost::scheduleReload()
{
    m_reloadTimer->start();
}

void ScriptHost::unloadAll()
{
    for (auto &s : m_scripts) {
        if (s->engine)
            s->engine->setInterrupted(true);
    }
    m_scripts.clear();
    m_columns.clear();
    m_cache.clear();
    m_queue.clear();
    m_queued.clear();
}

void ScriptHost::reload()
{
    unloadAll();
    QDir().mkpath(m_folder);
    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());
    if (!m_watcher->files().isEmpty())
        m_watcher->removePaths(m_watcher->files());
    m_watcher->addPath(m_folder);
    const QStringList files = QDir(m_folder).entryList({QStringLiteral("*.js")}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        const QString path = QDir(m_folder).filePath(f);
        m_watcher->addPath(path);
        load(path);
    }
    QList<CommandInfo> infos;
    for (const auto &s : m_scripts) {
        for (const Script::Command &c : std::as_const(s->commands)) {
            CommandInfo ci;
            ci.name = c.name;
            ci.summary = c.summary.isEmpty() ? QStringLiteral("From the script %1").arg(s->info.name)
                                             : QStringLiteral("%1 (script: %2)").arg(c.summary, s->info.name);
            for (const QString &a : c.args)
                ci.args << ArgInfo{a.toUpper(), {}, QString()};
            ci.example = c.name;
            infos << ci;
        }
    }
    CommandCatalog::setScriptCommands(infos);
    Q_EMIT scriptsChanged();
}

void ScriptHost::load(const QString &file)
{
    auto s = std::make_unique<Script>();
    s->info.file = file;
    s->info.id = QFileInfo(file).completeBaseName();
    s->info.name = s->info.id;
    s->info.enabled = !m_disabled.contains(s->info.id);
    s->varsFile = m_folder + QStringLiteral("/data/") + s->info.id + QStringLiteral(".json");
    Script *raw = s.get();
    m_scripts.push_back(std::move(s));
    if (!raw->info.enabled)
        return;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        raw->info.error = f.errorString();
        return;
    }
    const QString code = QString::fromUtf8(f.readAll());
    {
        QFile vf(raw->varsFile);
        if (vf.open(QIODevice::ReadOnly))
            raw->vars = QJsonDocument::fromJson(vf.readAll()).object().toVariantMap();
    }
    raw->engine = std::make_unique<QJSEngine>();
    raw->engine->installExtensions(QJSEngine::ConsoleExtension);
    raw->api = new ScriptApi(this, raw);
    raw->api->setParent(raw->engine.get());
    QJSEngine::setObjectOwnership(raw->api, QJSEngine::CppOwnership);
    raw->ctx = new ScriptCtx(raw, m_engine);
    raw->ctx->setParent(raw->engine.get());
    QJSEngine::setObjectOwnership(raw->ctx, QJSEngine::CppOwnership);
    raw->engine->globalObject().setProperty(QStringLiteral("dirduo"), raw->engine->newQObject(raw->api));
    m_watchdog->arm(raw->engine.get(), m_commandMs);
    const QJSValue r = raw->engine->evaluate(code, file);
    m_watchdog->disarm();
    if (raw->engine->isInterrupted()) {
        raw->engine->setInterrupted(false);
        raw->info.error = QStringLiteral("It took too long to start and was stopped.");
    } else if (r.isError()) {
        raw->info.error = QStringLiteral("Line %1: %2").arg(r.property(QStringLiteral("lineNumber")).toInt()).arg(r.toString());
    }
    if (!raw->info.error.isEmpty()) {
        addLog(raw->info.id, raw->info.error);
        // Nothing half-loaded stays: its commands and columns go.
        raw->commands.clear();
        raw->events.clear();
        for (const QString &c : std::as_const(raw->info.columns))
            m_columns.removeIf([&](const ScriptColumnDef &d) { return d.script == raw->info.id && d.name == c; });
        raw->columns.clear();
        raw->info.commands.clear();
        raw->info.columns.clear();
        raw->info.events.clear();
    }
}

QList<ScriptInfo> ScriptHost::scripts() const
{
    QList<ScriptInfo> out;
    for (const auto &s : m_scripts)
        out << s->info;
    return out;
}

void ScriptHost::saveDisabled()
{
    QSettings s(Paths::configFile(QStringLiteral("dirduo.ini")), QSettings::IniFormat);
    s.setValue(QStringLiteral("Scripts/disabled"), m_disabled);
}

void ScriptHost::setEnabled(const QString &id, bool on)
{
    if (on)
        m_disabled.removeAll(id);
    else if (!m_disabled.contains(id))
        m_disabled << id;
    saveDisabled();
    reload();
}

void ScriptHost::addLog(const QString &script, const QString &line)
{
    const QString entry = QStringLiteral("%1  %2: %3").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), script, line);
    m_log << entry;
    while (m_log.size() > 300)
        m_log.removeFirst();
    Q_EMIT logged(entry);
}

void ScriptHost::registerCommand(Script *s, const QJSValue &def)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z][A-Za-z0-9_]{0,40}$"));
    const QString name = def.property(QStringLiteral("name")).toString();
    const QJSValue run = def.property(QStringLiteral("run"));
    if (!valid.match(name).hasMatch()) {
        addLog(s->info.id, QStringLiteral("A command needs a name of letters and digits (got \"%1\").").arg(name));
        return;
    }
    if (!run.isCallable()) {
        addLog(s->info.id, QStringLiteral("The command %1 has no run function.").arg(name));
        return;
    }
    bool builtIn = false;
    for (const QString &n : CommandEngine::commandNames())
        builtIn = builtIn || n.compare(name, Qt::CaseInsensitive) == 0;
    if (builtIn || name.compare(QLatin1String(kAdminCommand), Qt::CaseInsensitive) == 0) {
        addLog(s->info.id, QStringLiteral("%1 is a built-in command; pick another name.").arg(name));
        return;
    }
    if (Script *other = scriptForCommand(name); other && other != s) {
        addLog(s->info.id, QStringLiteral("%1 is already a command of the script %2.").arg(name, other->info.name));
        return;
    }
    Script::Command c;
    c.name = name;
    c.summary = def.property(QStringLiteral("summary")).isUndefined() ? QString() : def.property(QStringLiteral("summary")).toString();
    c.args = def.property(QStringLiteral("args")).toVariant().toStringList();
    c.run = run;
    s->commands.insert(name.toLower(), c);
    if (!s->info.commands.contains(name))
        s->info.commands << name;
}

void ScriptHost::registerColumn(Script *s, const QJSValue &def)
{
    const QString name = def.property(QStringLiteral("name")).toString().trimmed();
    const QJSValue value = def.property(QStringLiteral("value"));
    if (name.isEmpty() || !value.isCallable()) {
        addLog(s->info.id, QStringLiteral("A column needs a name and a value function."));
        return;
    }
    QList<bool> used(kColumnSlots, false);
    for (const ScriptColumnDef &d : std::as_const(m_columns))
        used[d.slot] = true;
    const int slot = int(used.indexOf(false));
    if (slot < 0) {
        addLog(s->info.id, QStringLiteral("There is room for %1 script columns; %2 was left out.").arg(kColumnSlots).arg(name));
        return;
    }
    ScriptColumnDef d;
    d.script = s->info.id;
    d.name = name;
    const QJSValue label = def.property(QStringLiteral("label"));
    d.label = label.isUndefined() ? name : label.toString();
    d.numeric = def.property(QStringLiteral("numeric")).toBool();
    d.slot = slot;
    m_columns << d;
    s->columns.insert(name, value);
    s->info.columns << d.label;
}

void ScriptHost::registerEvent(Script *s, const QString &event, const QJSValue &fn)
{
    if (!kEvents.contains(event)) {
        addLog(s->info.id, QStringLiteral("There is no event \"%1\" (there are %2).").arg(event, kEvents.join(QStringLiteral(", "))));
        return;
    }
    if (!fn.isCallable()) {
        addLog(s->info.id, QStringLiteral("dirduo.on(\"%1\", ...) needs a function.").arg(event));
        return;
    }
    s->events[event] << fn;
    if (!s->info.events.contains(event))
        s->info.events << event;
}

QJSValue ScriptHost::call(Script *s, QJSValue fn, const QJSValueList &args, int timeoutMs, QString *error)
{
    m_watchdog->arm(s->engine.get(), timeoutMs);
    QJSValue r = fn.call(args);
    m_watchdog->disarm();
    if (s->engine->isInterrupted()) {
        s->engine->setInterrupted(false);
        if (error)
            *error = QStringLiteral("It ran longer than %1 seconds and was stopped.").arg(qMax(1, timeoutMs / 1000));
        return {};
    }
    if (r.isError()) {
        if (error)
            *error = QStringLiteral("Line %1: %2").arg(r.property(QStringLiteral("lineNumber")).toInt()).arg(r.toString());
        return {};
    }
    return r;
}

ScriptHost::Script *ScriptHost::scriptForCommand(const QString &name) const
{
    for (const auto &s : m_scripts) {
        if (s->commands.contains(name.toLower()))
            return s.get();
    }
    return nullptr;
}

bool ScriptHost::handles(const QString &name) const
{
    return name.compare(QLatin1String(kAdminCommand), Qt::CaseInsensitive) == 0 || scriptForCommand(name);
}

bool ScriptHost::runCommand(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (cmd.is(QLatin1String(kAdminCommand))) {
        if (cmd.hasFlag(QStringLiteral("RELOAD"))) {
            reload();
            return true;
        }
        if (cmd.hasArg(QStringLiteral("NEW"))) {
            QString err;
            const QString path = newScript(cmd.value(QStringLiteral("NEW")), &err);
            if (path.isEmpty())
                return fail(err);
            if (m_opener)
                m_opener(path);
            return true;
        }
        if (cmd.hasFlag(QStringLiteral("EXAMPLES"))) {
            addExamples();
            reload();
            ctx.afterLocalChange({});
            return true;
        }
        if (cmd.hasFlag(QStringLiteral("FOLDER"))) {
            QDir().mkpath(m_folder);
            ParsedCommand go;
            go.name = QStringLiteral("Go");
            go.positional << m_folder;
            return ctx.go(go, error);
        }
        return fail(QStringLiteral("Script takes RELOAD, NEW=<name>, EXAMPLES or FOLDER."));
    }
    Script *s = scriptForCommand(cmd.name);
    if (!s)
        return fail(QStringLiteral("Unknown command \"%1\".").arg(cmd.name));
    const Script::Command c = s->commands.value(cmd.name.toLower());
    s->ctx->bind(&ctx, cmd);
    QString err;
    const QJSValue r = call(s, c.run, {s->engine->newQObject(s->ctx)}, m_commandMs, &err);
    s->ctx->unbind();
    if (!err.isEmpty()) {
        addLog(s->info.id, QStringLiteral("%1: %2").arg(c.name, err));
        return fail(QStringLiteral("%1 (script %2)").arg(err, s->info.name));
    }
    // A command may return false to say it did not do its job (stops a button).
    return !(r.isBool() && !r.toBool());
}

QList<ScriptColumnDef> ScriptHost::columns() const
{
    return m_columns;
}

ScriptColumnDef ScriptHost::columnForSlot(int slot) const
{
    for (const ScriptColumnDef &d : m_columns) {
        if (d.slot == slot)
            return d;
    }
    return {};
}

bool ScriptHost::columnValue(int slot, const FileItem &item, QVariant *out)
{
    const QString key = QString::number(slot) + QLatin1Char('|') + item.path;
    const auto it = m_cache.constFind(key);
    if (it != m_cache.constEnd() && it->size == item.size && it->modified == item.modified) {
        if (out)
            *out = it->value;
        return true;
    }
    if (columnForSlot(slot).slot < 0) {
        if (out)
            *out = QVariant();
        return true;
    }
    if (!m_queued.contains(key)) {
        m_queued.insert(key, true);
        m_queue << Pending{slot, item};
        if (!m_queueTimer->isActive())
            m_queueTimer->start();
    }
    return false;
}

void ScriptHost::processColumnQueue()
{
    // A few at a time, so the lists keep scrolling while a folder fills in.
    QElapsedTimer t;
    t.start();
    QSet<QString> done;
    while (!m_queue.isEmpty() && t.elapsed() < 30) {
        const Pending p = m_queue.takeFirst();
        const QString key = QString::number(p.slot) + QLatin1Char('|') + p.item.path;
        m_queued.remove(key);
        const ScriptColumnDef d = columnForSlot(p.slot);
        QVariant value;
        Script *s = nullptr;
        for (const auto &sc : m_scripts) {
            if (sc->info.id == d.script)
                s = sc.get();
        }
        if (s && s->engine && s->columns.contains(d.name)) {
            QString err;
            const QJSValue r = call(s, s->columns.value(d.name), {itemFrom(s->engine.get(), p.item)}, m_columnMs, &err);
            if (!err.isEmpty()) {
                addLog(s->info.id, QStringLiteral("column %1, %2: %3").arg(d.label, p.item.name, err));
            } else if (r.isNumber()) {
                value = r.toNumber();
            } else if (!r.isUndefined() && !r.isNull()) {
                value = r.toString();
            }
        }
        m_cache.insert(key, CachedValue{value, p.item.size, p.item.modified});
        done.insert(p.item.path);
    }
    for (const QString &path : std::as_const(done))
        Q_EMIT columnValueReady(path);
    if (!m_queue.isEmpty())
        m_queueTimer->start();
}

void ScriptHost::fire(const QString &event, const QVariantMap &data, CommandContext *ctx)
{
    for (const auto &s : m_scripts) {
        const QList<QJSValue> handlers = s->events.value(event);
        if (handlers.isEmpty() || !s->engine)
            continue;
        if (ctx)
            s->ctx->bind(ctx, ParsedCommand{});
        for (const QJSValue &fn : handlers) {
            QJSValue e = s->engine->toScriptValue(data);
            e.setProperty(QStringLiteral("event"), event);
            QString err;
            call(s.get(), fn, {e, s->engine->newQObject(s->ctx)}, m_commandMs, &err);
            if (!err.isEmpty())
                addLog(s->info.id, QStringLiteral("%1: %2").arg(event, err));
        }
        s->ctx->unbind();
    }
}

QString ScriptHost::newScript(const QString &rawName, QString *error)
{
    QString name = rawName.trimmed();
    QString id;
    for (const QChar c : name.toLower()) {
        if (c.isLetterOrNumber())
            id += c;
        else if (!id.isEmpty() && !id.endsWith(QLatin1Char('-')))
            id += QLatin1Char('-');
    }
    while (id.endsWith(QLatin1Char('-')))
        id.chop(1);
    if (id.isEmpty()) {
        if (error)
            *error = QStringLiteral("Give the script a name.");
        return {};
    }
    QDir().mkpath(m_folder);
    const QString path = QDir(m_folder).filePath(id + QStringLiteral(".js"));
    if (QFileInfo::exists(path)) {
        if (error)
            *error = QStringLiteral("There is already a script called %1.js.").arg(id);
        return {};
    }
    QString command;
    for (const QString &w : name.split(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")), Qt::SkipEmptyParts))
        command += w.left(1).toUpper() + w.mid(1);
    if (command.isEmpty() || !command.at(0).isLetter())
        command = QStringLiteral("My") + command;
    const QString text = QStringLiteral(
        "// %1 - a DirDuo script. Saved changes load at once.\n"
        "// The README's Scripts section lists everything dirduo and ctx offer.\n"
        "\n"
        "dirduo.info({name: \"%1\", version: \"1.0\", description: \"What it does\"});\n"
        "\n"
        "// A command for buttons, menus and the console: %2 COUNT=3\n"
        "dirduo.command({\n"
        "    name: \"%2\",\n"
        "    summary: \"What the command does\",\n"
        "    args: [\"COUNT=\"],\n"
        "    run(ctx) {\n"
        "        const names = ctx.selected.map(item => item.name);\n"
        "        if (!names.length) {\n"
        "            ctx.message(\"Select something first.\");\n"
        "            return false;\n"
        "        }\n"
        "        ctx.message(names.length + \" selected in \" + ctx.source + \":\\n\" + names.join(\"\\n\"));\n"
        "    }\n"
        "});\n"
        "\n"
        "// A column (right-click a column header > Scripts):\n"
        "// dirduo.column({name: \"Words\", numeric: true, value(item) {\n"
        "//     return item.ext === \"txt\" ? (dirduo.readText(item.path) || \"\").split(/\\s+/).length : null;\n"
        "// }});\n"
        "\n"
        "// Events: folderOpened, jobFinished, started\n"
        "// dirduo.on(\"folderOpened\", (e, ctx) => dirduo.log(\"Opened \" + e.path));\n")
                             .arg(name, command);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    f.write(text.toUtf8());
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return {};
    }
    reload();
    return path;
}

QStringList ScriptHost::addExamples()
{
    const QList<QPair<QString, QString>> examples = {
        {QStringLiteral("rom-region.js"), QStringLiteral(R"JS(// ROM Region - a Region column for ROMs named the No-Intro way,
// "Super Metroid (Japan, USA).sfc", and a command to select one region.

dirduo.info({name: "ROM Region", version: "1.0",
             description: "Region column from (USA), (Europe), (Japan)... in ROM names; SelectRegion REGION=USA"});

const regions = ["USA", "Europe", "Japan", "World", "Korea", "Brazil", "Australia", "Asia",
                 "Germany", "France", "Spain", "Italy", "Netherlands", "Sweden", "China"];

function regionOf(name) {
    for (const tag of name.match(/\(([^)]*)\)/g) || []) {
        const hits = tag.slice(1, -1).split(/,\s*/).filter(p => regions.includes(p));
        if (hits.length)
            return hits.join(", ");
    }
    return "";
}

dirduo.column({name: "Region", value(item) { return item.isFolder ? "" : regionOf(item.name); }});

dirduo.command({
    name: "SelectRegion",
    summary: "Select the ROMs of one region",
    args: ["REGION="],
    run(ctx) {
        const want = ctx.args.REGION || ctx.ask("Region (USA, Europe, Japan...):", "USA");
        if (!want)
            return false;
        const paths = dirduo.list(ctx.source)
            .filter(i => !i.isFolder && regionOf(i.name).split(", ").includes(want))
            .map(i => i.path);
        if (!paths.length) {
            ctx.message("No " + want + " ROMs here.");
            return false;
        }
        ctx.select(paths);
    }
});
)JS")},
        {QStringLiteral("playlists.js"), QStringLiteral(R"JS(// Playlists - MakePlaylist writes an .m3u of the selected songs (or of every
// song in the folder when nothing is selected), next to them.

dirduo.info({name: "Playlists", version: "1.0", description: "MakePlaylist NAME=...: an .m3u of the selected music"});

const music = ["mp3", "flac", "ogg", "opus", "m4a", "aac", "wav", "aiff", "wma"];

dirduo.command({
    name: "MakePlaylist",
    summary: "An .m3u playlist of the selected songs, or the whole folder",
    args: ["NAME="],
    run(ctx) {
        let items = ctx.selected.filter(i => !i.isFolder);
        if (!items.length)
            items = dirduo.list(ctx.source);
        items = items.filter(i => music.includes(i.ext.toLowerCase()));
        if (!items.length) {
            ctx.message("There is no music here.");
            return false;
        }
        const name = ctx.args.NAME || ctx.ask("Playlist name:", dirduo.nameOf(ctx.source) || "Playlist");
        if (!name)
            return false;
        const file = dirduo.join(ctx.source, name.toLowerCase().endsWith(".m3u") ? name : name + ".m3u");
        // Paths relative to the playlist, so the folder can move.
        const lines = ["#EXTM3U"].concat(items.map(i => i.path.startsWith(ctx.source + "/")
                                                      ? i.path.slice(ctx.source.length + 1) : i.path));
        if (!dirduo.writeText(file, lines.join("\n") + "\n"))
            throw new Error("Could not write " + file);
        ctx.select([file]);
    }
});
)JS")},
        {QStringLiteral("recent-folders.js"), QStringLiteral(R"JS(// Recent Folders - remembers the last folders you opened (an event), and
// RecentFolders offers them to go back to (saved values and a choice).

dirduo.info({name: "Recent Folders", version: "1.0", description: "RecentFolders: pick one of the last 10 folders you opened"});

dirduo.on("folderOpened", e => {
    let list = dirduo.load("recent", []).filter(p => p !== e.path);
    list.unshift(e.path);
    dirduo.save("recent", list.slice(0, 10));
});

dirduo.command({
    name: "RecentFolders",
    summary: "Go to one of the last folders you opened",
    run(ctx) {
        const list = dirduo.load("recent", []).filter(p => dirduo.isFolder(p));
        if (!list.length) {
            ctx.message("No folders yet.");
            return false;
        }
        const i = ctx.choose("Go to:", list);
        if (i < 0)
            return false;
        ctx.run('Go "' + list[i] + '"');
    }
});
)JS")},
    };
    QDir().mkpath(m_folder);
    QStringList written;
    for (const auto &e : examples) {
        const QString path = QDir(m_folder).filePath(e.first);
        if (QFileInfo::exists(path))
            continue;
        QSaveFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(e.second.toUtf8());
            if (f.commit())
                written << path;
        }
    }
    return written;
}

#include "Scripting.moc"
