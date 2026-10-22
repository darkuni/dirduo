#pragma once

#include "core/CommandParser.h"
#include "core/Job.h"

#include <QObject>

class AliasManager;
class JobManager;

// What a command can see and do in the lister that ran it. The lister window
// implements this; tests use a fake.
class CommandContext
{
public:
    virtual ~CommandContext() = default;

    virtual QString sourcePath() const = 0;
    virtual QString destPath() const = 0;        // empty when there is no destination
    virtual QStringList selectedPaths() const = 0;
    virtual QString focusedPath() const = 0;

    virtual bool promptText(const QString &title, const QString &label, QString &value) = 0;
    virtual bool confirm(const QString &title, const QString &text) = 0;
    virtual void showError(const QString &title, const QString &text) = 0;
    virtual QString chooseDestination(const QString &title) = 0; // empty when cancelled

    // Commands that change the lister itself. cmd carries PATH= already
    // resolved to an absolute path where one was given.
    virtual bool go(const ParsedCommand &cmd, QString *error) = 0;
    virtual bool set(const ParsedCommand &cmd, QString *error) = 0;
    virtual bool select(const ParsedCommand &cmd, QString *error) = 0;
    virtual bool beginInlineRename() = 0;
    virtual bool toolbar(const ParsedCommand &cmd, QString *error)
    {
        Q_UNUSED(cmd)
        if (error)
            *error = QStringLiteral("This window has no toolbars.");
        return false;
    }

    // Something changed right away (rename, new folder): refresh, then select
    // these paths.
    virtual void afterLocalChange(const QStringList &selectPaths) = 0;
};

// Runs Opus-style commands. Every button, menu item, hotkey, drag and drop and
// inline rename goes through here, so anything clickable is scriptable.
//
// Commands in phase 1:
//   Go <path>|UP|BACK|FORWARD|REFRESH|SWAP [NEWTAB] [OPENINDEST]
//   Copy [MOVE] [AS] [FILE=...] [TO=...] [WHENEXISTS=ask|replace|newer|skip|rename]
//   Move ...                 (same as Copy MOVE)
//   Delete [NORECYCLE] [QUIET] [FILE=...]
//   Rename [FROM=... TO=...] (no arguments: rename the focused item in place)
//   CreateFolder [NAME="a, b, c"] [IN=...]
//   Clipboard COPY|CUT|PASTE
//   Set DUAL=on|off|toggle|vertical|horizontal  SOURCE=toggle  DESTLOCK=on|off|toggle
//       NAVLOCK=on|off|toggle
//       VIEW=details|list|icons  HIDDEN=on|off|toggle
//   Select ALL|NONE|INVERT|PATTERN=<wildcard>
//   Toolbar NAME=buttons|drives PLACE=top|bottom|left|right|between [NEWROW]
//           STATE=on|off|toggle
//
// Argument codes: {sourcepath} {destpath} {filepath} {file} {focusedpath}
// {dlgstring|Prompt text}. A trailing $ ({filepath$}) means the code is
// required: the function stops when it expands to nothing.
class CommandEngine : public QObject
{
    Q_OBJECT
public:
    CommandEngine(AliasManager &aliases, JobManager &jobs, QObject *parent = nullptr);

    // Runs every line in order; stops at the first failure and reports it
    // through ctx.showError (cancelled prompts stop silently).
    bool run(const QString &script, CommandContext &ctx);
    bool execute(const ParsedCommand &cmd, CommandContext &ctx, QString *error);

    // Shared with drag and drop and paste. Return the job id, or 0.
    int copyItems(const QStringList &sources, const QString &destDir, bool move,
                  CollisionAction whenExists = CollisionAction::Ask,
                  const QMap<QString, QString> &newNames = {});
    int trashItems(const QStringList &paths);
    int deleteItems(const QStringList &paths);

    // Alias expansion, then relative paths resolved against base.
    QString resolvePath(const QString &input, const QString &base) const;

    static QStringList commandNames();

    AliasManager &aliases() { return m_aliases; }
    JobManager &jobs() { return m_jobs; }

    // Set when the last run stopped because the user cancelled a prompt.
    static const QString kCancelled;

private:
    bool cmdGo(const ParsedCommand &cmd, CommandContext &ctx, QString *error);
    bool cmdCopy(const ParsedCommand &cmd, CommandContext &ctx, bool forceMove, QString *error);
    bool cmdDelete(const ParsedCommand &cmd, CommandContext &ctx, QString *error);
    bool cmdRename(const ParsedCommand &cmd, CommandContext &ctx, QString *error);
    bool cmdCreateFolder(const ParsedCommand &cmd, CommandContext &ctx, QString *error);
    bool cmdClipboard(const ParsedCommand &cmd, CommandContext &ctx, QString *error);

    QStringList sourcesFor(const ParsedCommand &cmd, CommandContext &ctx, const QStringList &flags) const;

    AliasManager &m_aliases;
    JobManager &m_jobs;
};
