#include "core/CommandEngine.h"

#include "core/AliasManager.h"
#include "core/FileOps.h"
#include "core/JobManager.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>

const QString CommandEngine::kCancelled = QStringLiteral("__cancelled__");

namespace {

const QStringList kGoFlags = {
    QStringLiteral("UP"), QStringLiteral("BACK"), QStringLiteral("FORWARD"), QStringLiteral("REFRESH"),
    QStringLiteral("SWAP"), QStringLiteral("NEWTAB"), QStringLiteral("OPENINDEST"),
};
const QStringList kCopyFlags = {QStringLiteral("MOVE"), QStringLiteral("AS")};
const QStringList kDeleteFlags = {QStringLiteral("NORECYCLE"), QStringLiteral("QUIET")};

bool parseWhenExists(const QString &value, CollisionAction &out)
{
    const QString v = value.toLower();
    if (v.isEmpty() || v == QLatin1String("ask"))
        out = CollisionAction::Ask;
    else if (v == QLatin1String("replace"))
        out = CollisionAction::Replace;
    else if (v == QLatin1String("newer") || v == QLatin1String("replaceifnewer"))
        out = CollisionAction::ReplaceIfNewer;
    else if (v == QLatin1String("skip"))
        out = CollisionAction::Skip;
    else if (v == QLatin1String("rename"))
        out = CollisionAction::Rename;
    else
        return false;
    return true;
}

QList<QUrl> toUrls(const QStringList &paths)
{
    QList<QUrl> urls;
    for (const QString &p : paths)
        urls << QUrl::fromLocalFile(p);
    return urls;
}

} // namespace

CommandEngine::CommandEngine(AliasManager &aliases, JobManager &jobs, QObject *parent)
    : QObject(parent)
    , m_aliases(aliases)
    , m_jobs(jobs)
{
}

QStringList CommandEngine::commandNames()
{
    return {QStringLiteral("Clipboard"), QStringLiteral("Copy"), QStringLiteral("CreateFolder"),
            QStringLiteral("Delete"), QStringLiteral("Go"), QStringLiteral("Move"),
            QStringLiteral("Rename"), QStringLiteral("Select"), QStringLiteral("Set")};
}

QString CommandEngine::resolvePath(const QString &input, const QString &base) const
{
    const QString resolved = m_aliases.resolve(input);
    if (resolved.isEmpty())
        return resolved;
    if (QDir::isAbsolutePath(resolved))
        return QDir::cleanPath(resolved);
    return QDir::cleanPath(QDir(base).filePath(resolved));
}

bool CommandEngine::run(const QString &script, CommandContext &ctx)
{
    QString error;
    // Codes are expanded once per function, at parse time, so every line sees
    // the selection as it was when the button was clicked.
    auto expander = [&ctx](const QString &rawCode, QStringList &out, bool &handled, QString *err) {
        QString code = rawCode;
        QString extra;
        const int bar = code.indexOf(QLatin1Char('|'));
        if (bar >= 0) {
            extra = code.mid(bar + 1);
            code = code.left(bar);
        }
        bool required = false;
        if (code.endsWith(QLatin1Char('$'))) {
            required = true;
            code.chop(1);
        }
        const QString c = code.toLower();
        handled = true;
        if (c == QLatin1String("sourcepath")) {
            out << ctx.sourcePath();
        } else if (c == QLatin1String("destpath")) {
            const QString d = ctx.destPath();
            if (!d.isEmpty())
                out << d;
        } else if (c == QLatin1String("filepath")) {
            out << ctx.selectedPaths();
        } else if (c == QLatin1String("file")) {
            for (const QString &p : ctx.selectedPaths())
                out << QFileInfo(p).fileName();
        } else if (c == QLatin1String("focusedpath")) {
            const QString f = ctx.focusedPath();
            if (!f.isEmpty())
                out << f;
        } else if (c == QLatin1String("dlgstring")) {
            QString value;
            if (!ctx.promptText(QStringLiteral("Linopus"), extra.isEmpty() ? QStringLiteral("Enter a value:") : extra, value)) {
                if (err)
                    *err = kCancelled;
                return false;
            }
            out << value;
        } else {
            handled = false;
            return true;
        }
        if (required && out.isEmpty()) {
            if (err)
                *err = c == QLatin1String("destpath") ? QStringLiteral("This needs a destination folder.")
                                                      : QStringLiteral("Select something first.");
            return false;
        }
        return true;
    };

    const QVector<ParsedCommand> commands = CommandParser::parse(script, expander, &error);
    if (commands.isEmpty()) {
        if (!error.isEmpty() && error != kCancelled)
            ctx.showError(QStringLiteral("Command failed"), error);
        return error.isEmpty();
    }
    for (const ParsedCommand &cmd : commands) {
        QString err;
        if (!execute(cmd, ctx, &err)) {
            if (!err.isEmpty() && err != kCancelled)
                ctx.showError(QStringLiteral("%1 failed").arg(cmd.name), err);
            return false;
        }
    }
    return true;
}

bool CommandEngine::execute(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    if (cmd.is(QStringLiteral("Go")))
        return cmdGo(cmd, ctx, error);
    if (cmd.is(QStringLiteral("Copy")))
        return cmdCopy(cmd, ctx, false, error);
    if (cmd.is(QStringLiteral("Move")))
        return cmdCopy(cmd, ctx, true, error);
    if (cmd.is(QStringLiteral("Delete")))
        return cmdDelete(cmd, ctx, error);
    if (cmd.is(QStringLiteral("Rename")))
        return cmdRename(cmd, ctx, error);
    if (cmd.is(QStringLiteral("CreateFolder")))
        return cmdCreateFolder(cmd, ctx, error);
    if (cmd.is(QStringLiteral("Clipboard")))
        return cmdClipboard(cmd, ctx, error);
    if (cmd.is(QStringLiteral("Set")))
        return ctx.set(cmd, error);
    if (cmd.is(QStringLiteral("Select")))
        return ctx.select(cmd, error);
    if (error)
        *error = QStringLiteral("Unknown command \"%1\".").arg(cmd.name);
    return false;
}

QStringList CommandEngine::sourcesFor(const ParsedCommand &cmd, CommandContext &ctx, const QStringList &flags) const
{
    QStringList named = cmd.values(QStringLiteral("FILE"));
    named << cmd.positionalExcept(flags);
    QStringList out;
    if (!named.isEmpty()) {
        for (const QString &n : std::as_const(named))
            out << resolvePath(n, ctx.sourcePath());
        return out;
    }
    return ctx.selectedPaths();
}

bool CommandEngine::cmdGo(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    ParsedCommand resolved = cmd;
    QStringList paths = cmd.values(QStringLiteral("PATH"));
    paths << cmd.positionalExcept(kGoFlags);
    if (!paths.isEmpty()) {
        const QString target = resolvePath(paths.first(), ctx.sourcePath());
        const QFileInfo fi(target);
        if (!fi.exists()) {
            if (error)
                *error = QStringLiteral("\"%1\" does not exist.").arg(paths.first());
            return false;
        }
        resolved.args[QStringLiteral("PATH")] = QStringList{fi.isDir() ? target : fi.absolutePath()};
        // Going to a file opens its folder and selects it.
        if (!fi.isDir())
            resolved.args[QStringLiteral("SELECT")] = QStringList{target};
    }
    return ctx.go(resolved, error);
}

int CommandEngine::copyItems(const QStringList &sources, const QString &destDir, bool move,
                             CollisionAction whenExists, const QMap<QString, QString> &newNames)
{
    if (sources.isEmpty() || destDir.isEmpty())
        return 0;
    JobSpec spec;
    spec.type = move ? JobType::Move : JobType::Copy;
    spec.sources = sources;
    spec.destDir = destDir;
    spec.whenExists = whenExists;
    spec.targetNames = newNames;
    return m_jobs.enqueue(spec);
}

int CommandEngine::trashItems(const QStringList &paths)
{
    if (paths.isEmpty())
        return 0;
    JobSpec spec;
    spec.type = JobType::Trash;
    spec.sources = paths;
    return m_jobs.enqueue(spec);
}

int CommandEngine::deleteItems(const QStringList &paths)
{
    if (paths.isEmpty())
        return 0;
    JobSpec spec;
    spec.type = JobType::Delete;
    spec.sources = paths;
    return m_jobs.enqueue(spec);
}

bool CommandEngine::cmdCopy(const ParsedCommand &cmd, CommandContext &ctx, bool forceMove, QString *error)
{
    const bool move = forceMove || cmd.hasFlag(QStringLiteral("MOVE"));
    const QStringList sources = sourcesFor(cmd, ctx, kCopyFlags);
    if (sources.isEmpty()) {
        // Pressing a key with nothing selected is not worth a dialog.
        if (error)
            *error = kCancelled;
        return false;
    }

    QString dest;
    if (cmd.hasArg(QStringLiteral("TO")))
        dest = resolvePath(cmd.value(QStringLiteral("TO")), ctx.sourcePath());
    else
        dest = ctx.destPath();
    if (dest.isEmpty())
        dest = ctx.chooseDestination(move ? QStringLiteral("Move to") : QStringLiteral("Copy to"));
    if (dest.isEmpty()) {
        if (error)
            *error = kCancelled;
        return false;
    }
    if (!QFileInfo(dest).isDir()) {
        if (error)
            *error = QStringLiteral("\"%1\" is not a folder.").arg(dest);
        return false;
    }

    CollisionAction whenExists = CollisionAction::Ask;
    if (!parseWhenExists(cmd.value(QStringLiteral("WHENEXISTS")), whenExists)) {
        if (error)
            *error = QStringLiteral("WHENEXISTS takes ask, replace, newer, skip or rename.");
        return false;
    }

    QMap<QString, QString> newNames;
    if (cmd.hasFlag(QStringLiteral("AS"))) {
        for (const QString &src : sources) {
            QString name = QFileInfo(src).fileName();
            const QString label = QStringLiteral("%1 \"%2\" as:").arg(move ? QStringLiteral("Move") : QStringLiteral("Copy"), name);
            if (!ctx.promptText(move ? QStringLiteral("Move As") : QStringLiteral("Copy As"), label, name)) {
                if (error)
                    *error = kCancelled;
                return false;
            }
            QString why;
            if (!FileOps::isValidFileName(name.trimmed(), &why)) {
                if (error)
                    *error = why;
                return false;
            }
            newNames.insert(QDir::cleanPath(src), name.trimmed());
        }
    }

    copyItems(sources, dest, move, whenExists, newNames);
    return true;
}

bool CommandEngine::cmdDelete(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    const QStringList sources = sourcesFor(cmd, ctx, kDeleteFlags);
    if (sources.isEmpty()) {
        if (error)
            *error = kCancelled;
        return false;
    }
    const bool permanent = cmd.hasFlag(QStringLiteral("NORECYCLE"));
    if (permanent) {
        if (!cmd.hasFlag(QStringLiteral("QUIET"))) {
            const QString what = sources.size() == 1 ? QStringLiteral("\"%1\"").arg(QFileInfo(sources.first()).fileName())
                                                      : QStringLiteral("these %1 items").arg(sources.size());
            if (!ctx.confirm(QStringLiteral("Delete permanently"),
                             QStringLiteral("Permanently delete %1? This cannot be undone.").arg(what))) {
                if (error)
                    *error = kCancelled;
                return false;
            }
        }
        deleteItems(sources);
    } else {
        trashItems(sources);
    }
    return true;
}

bool CommandEngine::cmdRename(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    if (!cmd.hasArg(QStringLiteral("FROM")) && !cmd.hasArg(QStringLiteral("TO")))
        return ctx.beginInlineRename();

    const QString from = cmd.hasArg(QStringLiteral("FROM")) ? resolvePath(cmd.value(QStringLiteral("FROM")), ctx.sourcePath())
                                                            : ctx.focusedPath();
    const QString to = cmd.value(QStringLiteral("TO"));
    if (from.isEmpty() || to.isEmpty()) {
        if (error)
            *error = QStringLiteral("Rename needs FROM= and TO=.");
        return false;
    }
    QString newPath;
    if (!FileOps::renameEntry(from, to, &newPath, error))
        return false;
    ctx.afterLocalChange({newPath});
    return true;
}

bool CommandEngine::cmdCreateFolder(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    const QString parent = cmd.hasArg(QStringLiteral("IN")) ? resolvePath(cmd.value(QStringLiteral("IN")), ctx.sourcePath())
                                                           : ctx.sourcePath();
    QString names = cmd.values(QStringLiteral("NAME")).join(QLatin1Char(','));
    if (names.isEmpty())
        names = cmd.positional.join(QLatin1Char(' '));
    if (names.trimmed().isEmpty()) {
        QString value = QStringLiteral("New Folder");
        if (!ctx.promptText(QStringLiteral("Create Folder"),
                            QStringLiteral("Folder name (separate several with commas):"), value)) {
            if (error)
                *error = kCancelled;
            return false;
        }
        names = value;
    }
    QString problems;
    const QStringList created = FileOps::createFolders(parent, names, &problems);
    if (!created.isEmpty())
        ctx.afterLocalChange(created);
    if (!problems.isEmpty()) {
        if (error)
            *error = problems;
        return false;
    }
    return true;
}

bool CommandEngine::cmdClipboard(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        if (error)
            *error = QStringLiteral("No clipboard is available.");
        return false;
    }
    const bool cut = cmd.hasFlag(QStringLiteral("CUT"));
    if (cmd.hasFlag(QStringLiteral("COPY")) || cut) {
        const QStringList paths = ctx.selectedPaths();
        if (paths.isEmpty()) {
            if (error)
                *error = kCancelled;
            return false;
        }
        auto *mime = new QMimeData;
        const QList<QUrl> urls = toUrls(paths);
        mime->setUrls(urls);
        // KDE and GNOME each mark a cut their own way; set both.
        mime->setData(QStringLiteral("application/x-kde-cutselection"), cut ? "1" : "0");
        QByteArray gnome = cut ? "cut" : "copy";
        for (const QUrl &u : urls)
            gnome += '\n' + u.toEncoded();
        mime->setData(QStringLiteral("x-special/gnome-copied-files"), gnome);
        clipboard->setMimeData(mime);
        return true;
    }
    if (cmd.hasFlag(QStringLiteral("PASTE"))) {
        const QMimeData *mime = clipboard->mimeData();
        if (!mime || !mime->hasUrls()) {
            if (error)
                *error = QStringLiteral("The clipboard holds no files.");
            return false;
        }
        QStringList sources;
        for (const QUrl &u : mime->urls()) {
            if (u.isLocalFile())
                sources << u.toLocalFile();
        }
        if (sources.isEmpty()) {
            if (error)
                *error = QStringLiteral("The clipboard holds no local files.");
            return false;
        }
        bool isCut = mime->data(QStringLiteral("application/x-kde-cutselection")) == "1";
        if (!isCut) {
            const QByteArray g = mime->data(QStringLiteral("x-special/gnome-copied-files"));
            isCut = g.startsWith("cut");
        }
        copyItems(sources, ctx.sourcePath(), isCut);
        if (isCut)
            clipboard->clear(); // a cut pastes once
        return true;
    }
    if (error)
        *error = QStringLiteral("Clipboard takes COPY, CUT or PASTE.");
    return false;
}
