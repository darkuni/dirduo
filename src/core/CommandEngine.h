#pragma once

#include "core/CommandParser.h"
#include "core/Job.h"
#include "core/History.h"
#include "core/Metadata.h"
#include "core/RenameEngine.h"

#include <QObject>

class AliasManager;
class QMimeData;
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
    virtual QString chooseDestination(const QString &title) = 0; // empty when canceled
    // One of several answers (index), or -1 when canceled.
    virtual int choose(const QString &title, const QString &text, const QStringList &choices)
    {
        Q_UNUSED(title)
        Q_UNUSED(text)
        Q_UNUSED(choices)
        return -1;
    }

    // Commands that change the lister itself. cmd carries PATH= already
    // resolved to an absolute path where one was given.
    virtual bool go(const ParsedCommand &cmd, QString *error) = 0;
    virtual bool set(const ParsedCommand &cmd, QString *error) = 0;
    virtual bool select(const ParsedCommand &cmd, QString *error) = 0;
    virtual bool beginInlineRename() = 0;
    // The Advanced Rename dialog over these items; options start from initial.
    virtual bool advancedRename(const QStringList &paths, const RenameOptions &initial, QString *error)
    {
        Q_UNUSED(paths)
        Q_UNUSED(initial)
        if (error)
            *error = QStringLiteral("Advanced Rename needs a lister window.");
        return false;
    }
    // Tools that open their own window (Sync, Duplicates).
    virtual bool tool(const ParsedCommand &cmd, QString *error)
    {
        Q_UNUSED(cmd)
        if (error)
            *error = QStringLiteral("This needs a lister window.");
        return false;
    }
    virtual bool settings(const ParsedCommand &cmd, QString *error)
    {
        Q_UNUSED(cmd)
        if (error)
            *error = QStringLiteral("Settings are not available here.");
        return false;
    }
    virtual bool printFolder(const ParsedCommand &cmd, QString *error)
    {
        Q_UNUSED(cmd)
        if (error)
            *error = QStringLiteral("This window has no folder listing to print.");
        return false;
    }
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
//   Rename ADVANCED | UNDO | OLDNAME=... NEWNAME=... [MODE=wildcard|findreplace|regexp]
//          [CASE=lower|upper|title|sentence] [EXTCASE=lower|upper] [IGNOREEXT]
//          [CASESENSITIVE] [START=n] [STEP=n] [WIDTH=n] [TRIM] [PRESET=name] [FILE=...]
//   CreateFolder [NAME="a, b, c"] [IN=...]
//   Clipboard COPY|CUT|PASTE [AS]  (a picture or text pastes as a new file)
//   Set DUAL=on|off|toggle|vertical|horizontal  SOURCE=toggle  DESTLOCK=on|off|toggle
//       NAVLOCK=on|off|toggle
//       VIEW=details|list|icons  HIDDEN=on|off|toggle
//   Select ALL|NONE|INVERT|PATTERN=<wildcard>
//   Settings BACKUP=<file|ask> | RESTORE=<file|ask> [QUIET]
//   Sync [LEFT=] [RIGHT=] [MODE=update|mirror|twoway] [COMPARE=date|size|datesize|contents]
//        [EXCLUDE=] [RUN]
//   Duplicates [IN=...] [BY=contents|namesize|name] [MINSIZE=bytes] [HIDDEN]
//   Collection ADD=<name> | REMOVE | CREATE=<name> | DELETE=<name> | OPEN=<name> [NEWTAB]
//   Print FOLDER [AS=text|csv|html] [TO=clipboard|ask|<file>] [SELECTED]
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
    // through ctx.showError (canceled prompts stop silently).
    bool run(const QString &script, CommandContext &ctx);
    bool execute(const ParsedCommand &cmd, CommandContext &ctx, QString *error);

    // Shared with drag and drop and paste. Return the job id, or 0.
    int copyItems(const QStringList &sources, const QString &destDir, bool move,
                  CollisionAction whenExists = CollisionAction::Ask,
                  const QMap<QString, QString> &newNames = {});
    // Items picked in a flat view of root: do some come from folders inside it?
    static bool fromSubfolders(const QStringList &sources, const QString &root);
    // Copies or moves items picked in a flat view of root. With keep, items
    // from folders inside root land in the same folders under destDir (made
    // as needed); otherwise they all land in destDir. Local destinations only
    // when keeping.
    bool copyItemsFrom(const QStringList &sources, const QString &root, const QString &destDir, bool move, bool keep,
                       CollisionAction whenExists, QString *error);
    // Asks whether to keep the folders (when some items come from inside
    // root) and copies. False when canceled or it failed (error set).
    bool copyFromFlat(const QStringList &sources, const QString &root, const QString &destDir, bool move,
                      CollisionAction whenExists, CommandContext &ctx, QString *error);
    int trashItems(const QStringList &paths);
    int deleteItems(const QStringList &paths);

    // Alias expansion, then relative paths resolved against base.
    QString resolvePath(const QString &input, const QString &base) const;

    static QStringList commandNames();

    AliasManager &aliases() { return m_aliases; }
    JobManager &jobs() { return m_jobs; }

    // Renames for undo: (new path, old path).
    void recordRename(const QList<QPair<QString, QString>> &done);
    bool canUndoRename() const { return m_history.latestRename() != nullptr; }
    // Writes tags to each file (its own edit each), with undo and the file
    // log; returns how many were written. problems lists the files that were not.
    int applyTags(const QList<QPair<QString, MediaEdit>> &edits, const QString &title, QStringList *problems);
    // Metadata COPY keeps these for PASTE.
    bool hasCopiedTags() const { return m_copiedTags.has_value(); }

    // Everything that can be undone, and the file log.
    UndoHistory &history() { return m_history; }

    // Set when the last run stopped because the user canceled a prompt.
    static const QString kCanceled;

private:
    bool cmdGo(const ParsedCommand &cmd, CommandContext &ctx, QString *error);
    bool cmdCopy(const ParsedCommand &cmd, CommandContext &ctx, bool forceMove, QString *error);
    bool cmdDelete(const ParsedCommand &cmd, CommandContext &ctx, QString *error);
    bool cmdRename(const ParsedCommand &cmd, CommandContext &ctx, QString *error);
    bool cmdCreateFolder(const ParsedCommand &cmd, CommandContext &ctx, QString *error);
    bool cmdClipboard(const ParsedCommand &cmd, CommandContext &ctx, QString *error);
    bool cmdCollection(const ParsedCommand &cmd, CommandContext &ctx, QString *error);
    bool pasteAsFile(const QMimeData *mime, bool askName, CommandContext &ctx, QString *error);
    bool cmdUndo(const ParsedCommand &cmd, CommandContext &ctx, QString *error);
    bool cmdMetadata(const ParsedCommand &cmd, CommandContext &ctx, QString *error);
    void recordNew(const QStringList &paths, const QString &what);

    QStringList sourcesFor(const ParsedCommand &cmd, CommandContext &ctx, const QStringList &flags) const;

    AliasManager &m_aliases;
    JobManager &m_jobs;
    UndoHistory m_history;
    std::optional<MediaInfo> m_copiedTags;
};
