#pragma once

#include "core/CommandParser.h"
#include "core/FileItem.h"

#include <QDateTime>
#include <QHash>
#include <QJSValue>
#include <QObject>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <memory>

class CommandContext;
class CommandEngine;
class QFileSystemWatcher;
class QTimer;

// One script as the Scripts page shows it.
struct ScriptInfo
{
    QString file;
    QString id; // its file name without .js
    QString name;
    QString version;
    QString description;
    QString author;
    bool enabled = true;
    QString error; // why it did not load, or its last failure
    QStringList commands;
    QStringList columns;
    QStringList events;
};

// A column a script adds; it takes one of the model's script column slots.
struct ScriptColumnDef
{
    QString script;
    QString name;
    QString label;
    int slot = -1;
    bool numeric = false;
};

// JavaScript scripts in ~/.config/dirduo/scripts, as Opus has (in
// JavaScript here): each file runs in its own engine and can add commands
// (usable in buttons, menus and the console like built-in ones), columns, and
// handlers for events. See README "Scripts" for the API.
//
//   dirduo.info({name: "ROM Tools", version: "1.0", description: "..."});
//   dirduo.command({name: "RomInfo", summary: "...", run(ctx) { ... }});
//   dirduo.column({name: "Region", value(item) { return "USA"; }});
//   dirduo.on("folderOpened", e => dirduo.log(e.path));
//
// Everything runs on the main thread; a script that takes too long (a few
// seconds for a command or an event, a fraction of one for a column value)
// is stopped.
class ScriptHost : public QObject
{
    Q_OBJECT
public:
    static constexpr int kColumnSlots = 6;
    static constexpr const char *kAdminCommand = "Script";

    explicit ScriptHost(QObject *parent = nullptr);
    ~ScriptHost() override;
    // The app's host (for the file list's columns); null when there is none.
    static ScriptHost *instance();

    // The engine that ctx.run() runs commands with.
    void setEngine(CommandEngine *engine) { m_engine = engine; }
    QString folder() const;
    void setFolder(const QString &dir); // tests
    void reload();
    QList<ScriptInfo> scripts() const;
    void setEnabled(const QString &id, bool on);
    // A new script from the template; its path, or empty with error.
    QString newScript(const QString &name, QString *error);
    // Writes the example scripts that are not there yet; their paths.
    QStringList addExamples();

    // Commands from scripts, and the Script command itself.
    bool handles(const QString &name) const;
    bool runCommand(const ParsedCommand &cmd, CommandContext &ctx, QString *error);

    QList<ScriptColumnDef> columns() const;
    ScriptColumnDef columnForSlot(int slot) const;
    // The value for an item, when worked out (cached); false while it is
    // being worked out, and columnValueReady follows.
    bool columnValue(int slot, const FileItem &item, QVariant *out);

    // folderOpened {path, display}, jobFinished {title, kind, ok, errors,
    // destination}, started {}.
    void fire(const QString &event, const QVariantMap &data, CommandContext *ctx);

    QStringList log() const { return m_log; }
    void setTimeouts(int commandMs, int columnMs); // tests

    // Lets the Script command open things (a new script, the folder).
    using Opener = std::function<void(const QString &path)>;
    void setOpener(Opener o) { m_opener = std::move(o); }

Q_SIGNALS:
    void scriptsChanged();
    void columnValueReady(const QString &path);
    void logged(const QString &line);

public:
    // Internal: used by the objects scripts see.
    struct Script;
    void addLog(const QString &script, const QString &line);
    void registerCommand(Script *s, const QJSValue &def);
    void registerColumn(Script *s, const QJSValue &def);
    void registerEvent(Script *s, const QString &event, const QJSValue &fn);
    QJSValue call(Script *s, QJSValue fn, const QJSValueList &args, int timeoutMs, QString *error);

private:
    void load(const QString &file);
    void unloadAll();
    void processColumnQueue();
    void scheduleReload();
    void saveDisabled();
    Script *scriptForCommand(const QString &name) const;

    QString m_folder;
    std::vector<std::unique_ptr<Script>> m_scripts;
    QStringList m_disabled;
    QStringList m_log;
    QList<ScriptColumnDef> m_columns;
    struct CachedValue
    {
        QVariant value;
        qint64 size = -1;
        QDateTime modified;
    };
    QHash<QString, CachedValue> m_cache; // "slot|path"
    struct Pending
    {
        int slot;
        FileItem item;
    };
    QList<Pending> m_queue;
    QHash<QString, bool> m_queued;
    QTimer *m_queueTimer;
    QTimer *m_reloadTimer;
    QFileSystemWatcher *m_watcher;
    int m_commandMs = 8000;
    int m_columnMs = 400;
    Opener m_opener;
    CommandEngine *m_engine = nullptr;
    class Watchdog;
    std::unique_ptr<Watchdog> m_watchdog;
};
