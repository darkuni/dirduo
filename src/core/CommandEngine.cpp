#include "core/CommandEngine.h"

#include "core/AliasManager.h"
#include "core/Archive.h"
#include "core/Attributes.h"
#include "core/HostRun.h"
#include "core/Servers.h"
#include "core/Apps.h"
#include "core/FileTags.h"
#include "core/Checksum.h"
#include "core/Prefs.h"
#include "core/Collections.h"
#include "core/FileOps.h"
#include "core/JobManager.h"
#include "core/Location.h"
#include "core/RemoteBackend.h"

#include <QClipboard>
#include <QSaveFile>
#include <QImage>
#include <QDateTime>
#include <QBuffer>
#include <QDir>
#include <QRegularExpression>
#include <QPointer>
#include <QHash>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>

const QString CommandEngine::kCanceled = QStringLiteral("__canceled__");

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


} // namespace

CommandEngine::CommandEngine(AliasManager &aliases, JobManager &jobs, QObject *parent)
    : QObject(parent)
    , m_aliases(aliases)
    , m_jobs(jobs)
    , m_history(jobs)
{
}

void CommandEngine::recordRename(const QList<QPair<QString, QString>> &done)
{
    for (const auto &d : done) {
        Collections::instance()->renamed(d.second, d.first);
        FileTags::renamed(d.second, d.first);
    }
    UndoEntry e;
    e.kind = UndoEntry::Kind::Rename;
    e.items = done;
    if (done.size() == 1)
        e.title = QStringLiteral("Rename %1 to %2").arg(Location::fileName(done.first().second), Location::fileName(done.first().first));
    else
        e.title = QStringLiteral("Rename %1 items").arg(done.size());
    m_history.add(e);
}

void CommandEngine::recordNew(const QStringList &paths, const QString &what)
{
    UndoEntry e;
    e.kind = UndoEntry::Kind::NewItem;
    for (const QString &p : paths) {
        if (Location::isLocal(p))
            e.items << qMakePair(p, p);
    }
    e.title = paths.size() == 1 ? QStringLiteral("New %1 %2").arg(what, Location::fileName(paths.first()))
                                : QStringLiteral("New %1s (%2)").arg(what).arg(paths.size());
    m_history.add(e);
}

QStringList CommandEngine::commandNames()
{
    return {QStringLiteral("Archive"), QStringLiteral("Checksum"), QStringLiteral("Clean"), QStringLiteral("Clipboard"), QStringLiteral("Collection"), QStringLiteral("Copy"), QStringLiteral("CreateFolder"),
            QStringLiteral("Delete"), QStringLiteral("Duplicates"), QStringLiteral("Favorite"), QStringLiteral("Find"), QStringLiteral("Go"),
            QStringLiteral("Image"), QStringLiteral("Join"),
            QStringLiteral("Layout"), QStringLiteral("MakeLink"), QStringLiteral("Metadata"), QStringLiteral("Move"), QStringLiteral("OpenWith"), QStringLiteral("Print"),
            QStringLiteral("Properties"), QStringLiteral("Rename"), QStringLiteral("Select"), QStringLiteral("Server"), QStringLiteral("Set"), QStringLiteral("SetAttr"), QStringLiteral("Settings"), QStringLiteral("Show"),
            QStringLiteral("Split"), QStringLiteral("Sync"), QStringLiteral("Tag"), QStringLiteral("Terminal"), QStringLiteral("Toolbar"), QStringLiteral("Undo")};
}

QString CommandEngine::resolvePath(const QString &input, const QString &base) const
{
    const QString resolved = m_aliases.resolve(input);
    if (resolved.isEmpty())
        return resolved;
    if (Location::isRemote(resolved) || QDir::isAbsolutePath(resolved))
        return Location::clean(resolved);
    return Location::join(base, resolved);
}

bool CommandEngine::run(const QString &script, CommandContext &ctx)
{
    if (ScriptRunner::isScript(script))
        return runScript(script, ctx);
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
                out << Location::fileName(p);
        } else if (c == QLatin1String("focusedpath")) {
            const QString f = ctx.focusedPath();
            if (!f.isEmpty())
                out << f;
        } else if (c == QLatin1String("dlgstring")) {
            QString value;
            if (!ctx.promptText(QStringLiteral("Linopus"), extra.isEmpty() ? QStringLiteral("Enter a value:") : extra, value)) {
                if (err)
                    *err = kCanceled;
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
        if (!error.isEmpty() && error != kCanceled)
            ctx.showError(QStringLiteral("Command failed"), error);
        return error.isEmpty();
    }
    for (const ParsedCommand &cmd : commands) {
        QString err;
        if (!execute(cmd, ctx, &err)) {
            if (!err.isEmpty() && err != kCanceled)
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
    if (cmd.is(QStringLiteral("Collection")))
        return cmdCollection(cmd, ctx, error);
    if (cmd.is(QStringLiteral("Set")))
        return ctx.set(cmd, error);
    if (cmd.is(QStringLiteral("Select")))
        return ctx.select(cmd, error);
    if (cmd.is(QStringLiteral("Toolbar")))
        return ctx.toolbar(cmd, error);
    if (cmd.is(QStringLiteral("Print")))
        return ctx.printFolder(cmd, error);
    if (cmd.is(QStringLiteral("Settings")))
        return ctx.settings(cmd, error);
    if (cmd.is(QStringLiteral("Undo")))
        return cmdUndo(cmd, ctx, error);
    if (cmd.is(QStringLiteral("Metadata")))
        return cmdMetadata(cmd, ctx, error);
    if (cmd.is(QStringLiteral("Archive")))
        return cmdArchive(cmd, ctx, error);
    if (cmd.is(QStringLiteral("SetAttr")))
        return cmdSetAttr(cmd, ctx, error);
    if (cmd.is(QStringLiteral("Properties")))
        return ctx.tool(cmd, error);
    if (cmd.is(QStringLiteral("MakeLink")))
        return cmdMakeLink(cmd, ctx, error);
    if (cmd.is(QStringLiteral("Terminal")))
        return cmdTerminal(cmd, ctx, error);
    if (cmd.is(QStringLiteral("Server")))
        return cmdServer(cmd, ctx, error);
    if (cmd.is(QStringLiteral("OpenWith")))
        return cmdOpenWith(cmd, ctx, error);
    if (cmd.is(QStringLiteral("Tag")))
        return cmdTag(cmd, ctx, error);
    if (cmd.is(QStringLiteral("Clean")))
        return cmdClean(cmd, ctx, error);
    if (cmd.is(QStringLiteral("Split")) || cmd.is(QStringLiteral("Join")))
        return cmdSplitJoin(cmd, ctx, error);
    if (cmd.is(QStringLiteral("Checksum"))) {
        if (cmd.hasArg(QStringLiteral("ALGO")) && !Checksum::fromName(cmd.value(QStringLiteral("ALGO")), nullptr)) {
            if (error)
                *error = QStringLiteral("ALGO takes md5, sha1, sha256 or crc32.");
            return false;
        }
        return ctx.tool(cmd, error); // the Checksums window
    }
    if (cmd.is(QStringLiteral("Show")))
        return ctx.tool(cmd, error); // the picture viewer
    if (cmd.is(QStringLiteral("Sync")) || cmd.is(QStringLiteral("Duplicates")) || cmd.is(QStringLiteral("Find"))
        || cmd.is(QStringLiteral("Favorite")) || cmd.is(QStringLiteral("Layout")) || cmd.is(QStringLiteral("Image")))
        return ctx.tool(cmd, error);
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
        if (Location::isRemote(target)) {
            // Whether it exists is the server's to say; the display reports it.
            resolved.args[QStringLiteral("PATH")] = QStringList{target};
            return ctx.go(resolved, error);
        }
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
    spec.askOnError = m_interactive && Prefs::instance()->values().askOnError;
    return m_jobs.enqueue(spec);
}

bool CommandEngine::fromSubfolders(const QStringList &sources, const QString &root)
{
    if (root.isEmpty())
        return false;
    const QString base = Location::clean(root);
    const QString prefix = base.endsWith(QLatin1Char('/')) ? base : base + QLatin1Char('/');
    for (const QString &s : sources) {
        const QString parent = Location::parent(s);
        if (parent != base && parent.startsWith(prefix))
            return true;
    }
    return false;
}

bool CommandEngine::copyItemsFrom(const QStringList &sources, const QString &root, const QString &destDir, bool move,
                                  bool keep, CollisionAction whenExists, QString *error)
{
    if (!keep) {
        copyItems(sources, destDir, move, whenExists);
        return true;
    }
    if (!Location::isLocal(destDir)) {
        if (error)
            *error = QStringLiteral("Keeping the folder structure works with folders on this computer and its cards for now. "
                                    "Choose Same Folder to send them all to the server folder.");
        return false;
    }
    const QString base = Location::clean(root);
    const QString prefix = base.endsWith(QLatin1Char('/')) ? base : base + QLatin1Char('/');
    // A folder carries what is inside it: drop items that sit in another picked folder.
    QStringList picked;
    for (const QString &s : sources)
        picked << Location::clean(s);
    QStringList kept;
    for (const QString &s : std::as_const(picked)) {
        bool inside = false;
        for (const QString &other : std::as_const(picked)) {
            if (other != s && s.startsWith(other + QLatin1Char('/'))) {
                inside = true;
                break;
            }
        }
        if (!inside)
            kept << s;
    }
    // One job per folder, in the order the folders first appear.
    QStringList order;
    QHash<QString, QStringList> groups;
    for (const QString &s : std::as_const(kept)) {
        const QString parent = Location::parent(s);
        const QString rel = parent.startsWith(prefix) ? parent.mid(prefix.size()) : QString();
        if (!groups.contains(rel))
            order << rel;
        groups[rel] << s;
    }
    const QString dest = QDir::cleanPath(destDir);
    for (const QString &rel : std::as_const(order)) {
        const QString target = rel.isEmpty() ? dest : dest + QLatin1Char('/') + rel;
        if (!QDir().mkpath(target)) {
            if (error)
                *error = QStringLiteral("Could not make the folder \"%1\".").arg(target);
            return false;
        }
        copyItems(groups.value(rel), target, move, whenExists);
    }
    return true;
}

bool CommandEngine::copyFromFlat(const QStringList &sources, const QString &root, const QString &destDir, bool move,
                                 CollisionAction whenExists, CommandContext &ctx, QString *error)
{
    if (!fromSubfolders(sources, root)) {
        copyItems(sources, destDir, move, whenExists);
        return true;
    }
    const QString verb = move ? QStringLiteral("Move") : QStringLiteral("Copy");
    const int answer = ctx.choose(
        verb,
        QStringLiteral("Some of these come from folders inside \"%1\".\n\n"
                       "Keep Folder Structure makes the same folders at the destination.\n"
                       "Same Folder puts everything straight into the destination.")
            .arg(Location::fileName(root)),
        {QStringLiteral("Keep Folder Structure"), QStringLiteral("Same Folder")});
    if (answer < 0) {
        if (error)
            *error = kCanceled;
        return false;
    }
    return copyItemsFrom(sources, root, destDir, move, answer == 0, whenExists, error);
}

QString CommandEngine::suggestedArchiveName(const QStringList &sources, const QString &folder, const QString &formatKey)
{
    Archive::Format f = Archive::Format::Zip;
    Archive::formatFromKey(formatKey, f);
    QString base;
    if (sources.size() == 1) {
        const QFileInfo fi(sources.first());
        base = fi.isDir() ? fi.fileName() : fi.completeBaseName();
        if (base.isEmpty())
            base = fi.fileName(); // ".hidden"
    } else {
        base = Location::fileName(folder);
    }
    if (base.isEmpty())
        base = QStringLiteral("Archive");
    return base + QLatin1Char('.') + Archive::extensionFor(f);
}

bool CommandEngine::cmdArchive(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (!Archive::available())
        return fail(QStringLiteral("This build of Linopus was made without archive support."));
    const QStringList flags = {QStringLiteral("CREATE"), QStringLiteral("EXTRACT"), QStringLiteral("DEST"), QStringLiteral("HERE"), QStringLiteral("ASK"), QStringLiteral("QUICK"), QStringLiteral("TRASH")};
    QStringList sources = sourcesFor(cmd, ctx, flags);
    CollisionAction whenExists = CollisionAction::Rename;
    if (cmd.hasArg(QStringLiteral("WHENEXISTS")) && !parseWhenExists(cmd.value(QStringLiteral("WHENEXISTS")), whenExists))
        return fail(QStringLiteral("WHENEXISTS takes replace, skip or rename."));
    if (whenExists == CollisionAction::Ask || whenExists == CollisionAction::ReplaceIfNewer)
        whenExists = CollisionAction::Rename;

    // Where to: TO=<folder>, DEST (the other display), else the source folder.
    QString dest;
    if (cmd.hasArg(QStringLiteral("TO")) && !cmd.value(QStringLiteral("TO")).isEmpty())
        dest = resolvePath(cmd.value(QStringLiteral("TO")), ctx.sourcePath());
    else if (cmd.hasFlag(QStringLiteral("DEST"))) {
        dest = ctx.destPath();
        if (dest.isEmpty())
            return fail(QStringLiteral("There is no destination: turn on the second display or give TO=."));
    } else
        dest = ctx.sourcePath();
    auto checkLocal = [&](const QStringList &paths) {
        for (const QString &p : paths) {
            if (!Location::isLocal(p))
                return false;
        }
        return Location::isLocal(dest);
    };

    if (cmd.hasFlag(QStringLiteral("EXTRACT")) || cmd.hasArg(QStringLiteral("EXTRACT"))) {
        QStringList archives;
        for (const QString &p : std::as_const(sources)) {
            if (Archive::isArchive(p))
                archives << p;
        }
        if (archives.isEmpty())
            return fail(sources.isEmpty() ? QStringLiteral("Select an archive to extract first.")
                                          : QStringLiteral("None of the selected files is an archive this can extract."));
        if (!checkLocal(archives))
            return fail(QStringLiteral("Archives are extracted from and into folders on this computer and its cards for now."));
        JobSpec spec;
        spec.type = JobType::Unpack;
        spec.sources = archives;
        spec.destDir = dest;
        spec.whenExists = whenExists == CollisionAction::Replace ? CollisionAction::Replace : CollisionAction::Rename;
        const QString mode = cmd.value(QStringLiteral("MODE")).toLower();
        if (mode.isEmpty() || mode == QLatin1String("smart"))
            spec.extractMode = JobSpec::ExtractMode::Smart;
        else if (mode == QLatin1String("folder"))
            spec.extractMode = JobSpec::ExtractMode::Folder;
        else if (mode == QLatin1String("here"))
            spec.extractMode = JobSpec::ExtractMode::Here;
        else
            return fail(QStringLiteral("MODE takes smart, folder or here."));
        spec.trashAfter = cmd.hasFlag(QStringLiteral("TRASH"));
        // Encrypted: ask once for them all (PASSWORD= answers in advance).
        spec.password = cmd.value(QStringLiteral("PASSWORD"));
        if (spec.password.isEmpty()) {
            QStringList locked;
            for (const QString &p : std::as_const(archives)) {
                if (Archive::isEncrypted(p))
                    locked << Location::fileName(p);
            }
            if (!locked.isEmpty()) {
                const QString label = locked.size() == 1
                    ? QStringLiteral("\"%1\" is protected by a password:").arg(locked.first())
                    : QStringLiteral("%1 of these archives are protected by a password:").arg(locked.size());
                if (!ctx.promptPassword(QStringLiteral("Extract"), label, spec.password) || spec.password.isEmpty()) {
                    if (error)
                        *error = kCanceled;
                    return false;
                }
            }
        }
        m_jobs.enqueue(spec);
        return true;
    }

    if (cmd.hasFlag(QStringLiteral("CREATE")) || cmd.hasArg(QStringLiteral("CREATE"))) {
        if (sources.isEmpty())
            return fail(QStringLiteral("Select the files and folders to put in the archive first."));
        QString name = cmd.value(QStringLiteral("NAME"));
        if (name.isEmpty())
            name = cmd.value(QStringLiteral("CREATE"));
        if (name.isEmpty() && cmd.hasFlag(QStringLiteral("QUICK"))) {
            const QString key = cmd.value(QStringLiteral("FORMAT")).isEmpty() ? QStringLiteral("zip") : cmd.value(QStringLiteral("FORMAT")).toLower();
            name = suggestedArchiveName(sources, ctx.sourcePath(), key);
        }
        // No name: the Add to Archive dialog asks (it runs this again with one).
        if (name.isEmpty() || cmd.hasFlag(QStringLiteral("ASK")))
            return ctx.tool(cmd, error);
        if (!checkLocal(sources))
            return fail(QStringLiteral("Archives are made from and into folders on this computer and its cards for now."));
        Archive::Format format;
        QString key = cmd.value(QStringLiteral("FORMAT")).toLower();
        if (!key.isEmpty()) {
            if (!Archive::formatFromKey(key, format)) {
                QStringList keys;
                for (Archive::Format f : Archive::formats())
                    keys << Archive::extensionFor(f);
                return fail(QStringLiteral("FORMAT takes %1.").arg(keys.join(QStringLiteral(", "))));
            }
            Archive::Format named;
            if (!Archive::formatFromName(name, named) || named != format)
                name += QLatin1Char('.') + Archive::extensionFor(format);
        } else if (!Archive::formatFromName(name, format)) {
            format = Archive::Format::Zip;
            name += QStringLiteral(".zip");
        }
        QString why;
        if (!FileOps::isValidFileName(name, &why))
            return fail(why);
        JobSpec spec;
        spec.type = JobType::Pack;
        spec.sources = sources;
        spec.destDir = dest;
        spec.archiveName = name;
        spec.archiveFormat = Archive::extensionFor(format);
        spec.whenExists = whenExists;
        spec.password = cmd.value(QStringLiteral("PASSWORD"));
        if (!spec.password.isEmpty() && !Archive::canEncrypt(format))
            return fail(QStringLiteral("Only zip archives can have a password."));
        m_jobs.enqueue(spec);
        return true;
    }
    return fail(QStringLiteral("Archive takes CREATE or EXTRACT."));
}

bool CommandEngine::cmdSetAttr(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    const QStringList flags = {QStringLiteral("RECURSE")};
    const QStringList paths = sourcesFor(cmd, ctx, flags);
    if (paths.isEmpty())
        return fail(QStringLiteral("Select something first."));
    Attributes::Change change;
    change.recurse = cmd.hasFlag(QStringLiteral("RECURSE"));
    if (cmd.hasArg(QStringLiteral("MODIFIED"))) {
        const QDateTime t = Attributes::parseDate(cmd.value(QStringLiteral("MODIFIED")));
        if (!t.isValid())
            return fail(QStringLiteral("MODIFIED takes a date like 2024-05-01 13:30, or now."));
        change.modified = t;
    }
    if (cmd.hasArg(QStringLiteral("ACCESSED"))) {
        const QDateTime t = Attributes::parseDate(cmd.value(QStringLiteral("ACCESSED")));
        if (!t.isValid())
            return fail(QStringLiteral("ACCESSED takes a date like 2024-05-01 13:30, or now."));
        change.accessed = t;
    }
    if (cmd.hasArg(QStringLiteral("PERMS"))) {
        if (!Attributes::parseMode(cmd.value(QStringLiteral("PERMS")), change.mode))
            return fail(QStringLiteral("PERMS takes 644, rwxr-xr-x, or changes like u+x,go-w."));
    }
    // Nothing to change: the Properties window.
    if (change.isEmpty())
        return ctx.tool(cmd, error);
    for (const QString &p : paths) {
        if (!Location::isLocal(p))
            return fail(QStringLiteral("Permissions and dates are changed on this computer and its cards for now."));
    }
    QStringList problems;
    Attributes::apply(paths, change, &problems);
    ctx.afterLocalChange({});
    if (!problems.isEmpty()) {
        const QString shown = problems.mid(0, 10).join(QLatin1Char('\n'));
        return fail(problems.size() > 10 ? shown + QStringLiteral("\n...and %1 more.").arg(problems.size() - 10) : shown);
    }
    return true;
}

bool CommandEngine::cmdMakeLink(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    const QStringList flags = {QStringLiteral("DEST"), QStringLiteral("RELATIVE"), QStringLiteral("HERE")};
    const QStringList targets = sourcesFor(cmd, ctx, flags);
    if (targets.isEmpty())
        return fail(QStringLiteral("Select what the link should point to first."));
    // Where: TO=, DEST, else beside the item ("Name (link)").
    QString folder;
    if (cmd.hasArg(QStringLiteral("TO")))
        folder = resolvePath(cmd.value(QStringLiteral("TO")), ctx.sourcePath());
    else if (cmd.hasFlag(QStringLiteral("DEST"))) {
        folder = ctx.destPath();
        if (folder.isEmpty())
            return fail(QStringLiteral("There is no destination: turn on the second display or give TO=."));
    }
    if (!folder.isEmpty() && (!Location::isLocal(folder) || !QFileInfo(folder).isDir()))
        return fail(QStringLiteral("Links are made in folders on this computer and its cards."));
    const QString name = cmd.value(QStringLiteral("NAME"));
    if (!name.isEmpty() && targets.size() > 1)
        return fail(QStringLiteral("NAME= works with one item at a time."));
    QStringList made;
    for (const QString &target : targets) {
        if (!Location::isLocal(target))
            return fail(QStringLiteral("Links point to items on this computer and its cards."));
        const QFileInfo fi(target);
        const QString dir = folder.isEmpty() ? fi.absolutePath() : folder;
        QString linkName = name;
        if (linkName.isEmpty()) {
            linkName = fi.fileName();
            if (dir == fi.absolutePath()) {
                // Beside the original: "Game (link)", "notes (link).txt".
                const QString suffix = fi.isDir() ? QString() : fi.suffix();
                const QString base = suffix.isEmpty() ? fi.fileName() : fi.completeBaseName();
                linkName = base + QStringLiteral(" (link)") + (suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix);
            }
            if (QFileInfo::exists(dir + QLatin1Char('/') + linkName) || QFileInfo(dir + QLatin1Char('/') + linkName).isSymLink())
                linkName = FileOps::uniqueName(dir, linkName);
        }
        QString why;
        if (!FileOps::isValidFileName(linkName, &why))
            return fail(why);
        const QString linkPath = dir + QLatin1Char('/') + linkName;
        if (!Attributes::makeLink(target, linkPath, cmd.hasFlag(QStringLiteral("RELATIVE")), &why))
            return fail(why);
        made << linkPath;
    }
    recordNew(made, QStringLiteral("link"));
    ctx.afterLocalChange(folder.isEmpty() ? made : QStringList());
    return true;
}

bool CommandEngine::cmdTerminal(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    // In the folder given, else the source folder.
    QString dir = cmd.value(QStringLiteral("PATH"));
    if (dir.isEmpty())
        dir = cmd.positionalExcept({}).value(0);
    dir = dir.isEmpty() ? ctx.sourcePath() : resolvePath(dir, ctx.sourcePath());
    if (!Location::isLocal(dir) || !QFileInfo(dir).isDir()) {
        if (error)
            *error = QStringLiteral("A terminal opens in folders on this computer and its cards.");
        return false;
    }
    QString program = cmd.value(QStringLiteral("PROGRAM"));
    if (program.isEmpty())
        program = Prefs::instance()->values().terminal; // Preferences > General
    return HostRun::openTerminal(dir, program, error);
}

bool CommandEngine::cmdServer(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    Servers *servers = Servers::instance();
    if (cmd.hasArg(QStringLiteral("GO"))) {
        const QString name = cmd.value(QStringLiteral("GO"));
        const QString url = servers->urlFor(name);
        if (url.isEmpty())
            return fail(QStringLiteral("There is no server called \"%1\".").arg(name));
        ParsedCommand go;
        go.name = QStringLiteral("Go");
        go.positional << url;
        for (const QString &f : {QStringLiteral("NEWTAB"), QStringLiteral("OPENINDEST")}) {
            if (cmd.hasFlag(f))
                go.positional << f;
        }
        return cmdGo(go, ctx, error);
    }
    if (cmd.hasArg(QStringLiteral("REMOVE"))) {
        if (!servers->remove(cmd.value(QStringLiteral("REMOVE"))))
            return fail(QStringLiteral("There is no server called \"%1\".").arg(cmd.value(QStringLiteral("REMOVE"))));
        return true;
    }
    if (cmd.hasFlag(QStringLiteral("ADD")) && cmd.hasArg(QStringLiteral("URL"))) {
        const QString url = cmd.value(QStringLiteral("URL"));
        QString name = cmd.value(QStringLiteral("NAME"));
        if (name.isEmpty())
            name = Servers::suggestedName(url);
        return servers->add(Server{name, url}, error);
    }
    // ADD without an address, or nothing: the window (Add Server, or the list).
    return ctx.tool(cmd, error);
}

bool CommandEngine::applyFileTags(const QList<QPair<QString, FileTagData>> &changes, const QString &title, QStringList *problems)
{
    UndoEntry undo;
    undo.kind = UndoEntry::Kind::Labels;
    undo.title = title;
    for (const auto &[path, data] : changes) {
        const FileTagData before = FileTags::read(path);
        if (before == data)
            continue;
        QString why;
        if (!FileTags::write(path, data, &why)) {
            if (problems)
                *problems << QStringLiteral("%1: %2").arg(Location::fileName(path), why);
            continue;
        }
        undo.items << qMakePair(path, path);
        undo.before << before.toMap();
    }
    if (!undo.items.isEmpty())
        m_history.add(undo);
    return problems == nullptr || problems->isEmpty();
}

bool CommandEngine::cmdTag(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    const QStringList flags = {QStringLiteral("CLEAR"), QStringLiteral("CLEARALL")};
    QStringList paths;
    for (const QString &p : sourcesFor(cmd, ctx, flags)) {
        if (!Location::isLocal(p))
            return fail(QStringLiteral("Labels and tags work with items on this computer and its cards for now."));
        paths << p;
    }
    if (paths.isEmpty())
        return fail(QStringLiteral("Select something first."));
    const bool anything = cmd.hasArg(QStringLiteral("LABEL")) || cmd.hasArg(QStringLiteral("RATING")) || cmd.hasArg(QStringLiteral("ADD"))
        || cmd.hasArg(QStringLiteral("REMOVE")) || cmd.hasArg(QStringLiteral("SET")) || cmd.hasArg(QStringLiteral("COMMENT"))
        || cmd.hasArg(QStringLiteral("DESCRIPTION")) || cmd.hasFlag(QStringLiteral("CLEAR")) || cmd.hasFlag(QStringLiteral("CLEARALL"));
    if (!anything)
        return ctx.tool(cmd, error); // the Tags window

    auto list = [](const QString &v) {
        QStringList out;
        for (const QString &t : v.split(QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts))
            if (!t.trimmed().isEmpty())
                out << t.trimmed();
        return out;
    };
    QString label;
    const bool setLabel = cmd.hasArg(QStringLiteral("LABEL"));
    if (setLabel) {
        label = cmd.value(QStringLiteral("LABEL")).trimmed();
        if (label.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
            label.clear();
        else {
            bool known = false;
            for (const LabelDef &d : FileTags::labels()) {
                if (d.name.compare(label, Qt::CaseInsensitive) == 0) {
                    label = d.name;
                    known = true;
                }
            }
            if (!known) {
                QStringList names;
                for (const LabelDef &d : FileTags::labels())
                    names << d.name;
                return fail(QStringLiteral("LABEL takes none or one of: %1.").arg(names.join(QStringLiteral(", "))));
            }
        }
    }
    int rating = -1;
    int ratingStep = 0;
    if (cmd.hasArg(QStringLiteral("RATING"))) {
        const QString v = cmd.value(QStringLiteral("RATING")).trimmed();
        bool ok = false;
        if (v == QLatin1String("+") || v == QLatin1String("up"))
            ratingStep = 1;
        else if (v == QLatin1String("-") || v == QLatin1String("down"))
            ratingStep = -1;
        else {
            rating = v.toInt(&ok);
            if (!ok || rating < 0 || rating > 5)
                return fail(QStringLiteral("RATING takes 0 to 5 stars, + or -."));
        }
    }
    QList<QPair<QString, FileTagData>> changes;
    for (const QString &p : std::as_const(paths)) {
        FileTagData d = FileTags::read(p);
        if (cmd.hasFlag(QStringLiteral("CLEARALL")))
            d = FileTagData{};
        if (setLabel)
            d.label = label;
        if (rating >= 0)
            d.rating = rating;
        if (ratingStep != 0)
            d.rating = qBound(0, d.rating + ratingStep, 5);
        if (cmd.hasFlag(QStringLiteral("CLEAR")))
            d.tags.clear();
        if (cmd.hasArg(QStringLiteral("SET")))
            d.tags = list(cmd.value(QStringLiteral("SET")));
        for (const QString &t : list(cmd.values(QStringLiteral("ADD")).join(QLatin1Char(',')))) {
            if (!d.tags.contains(t, Qt::CaseInsensitive))
                d.tags << t;
        }
        for (const QString &t : list(cmd.values(QStringLiteral("REMOVE")).join(QLatin1Char(','))))
            d.tags.removeIf([&t](const QString &x) { return x.compare(t, Qt::CaseInsensitive) == 0; });
        if (cmd.hasArg(QStringLiteral("COMMENT")))
            d.comment = cmd.value(QStringLiteral("COMMENT"));
        if (cmd.hasArg(QStringLiteral("DESCRIPTION")))
            d.comment = cmd.value(QStringLiteral("DESCRIPTION"));
        changes << qMakePair(p, d);
    }
    QString what = setLabel ? (label.isEmpty() ? QStringLiteral("Remove label") : QStringLiteral("Label %1").arg(label))
        : (rating >= 0 || ratingStep) ? QStringLiteral("Rate")
                                      : QStringLiteral("Tag");
    const QString title = paths.size() == 1 ? QStringLiteral("%1: %2").arg(what, Location::fileName(paths.first()))
                                            : QStringLiteral("%1: %2 items").arg(what).arg(paths.size());
    QStringList problems;
    applyFileTags(changes, title, &problems);
    ctx.afterLocalChange(paths);
    if (!problems.isEmpty())
        return fail(problems.mid(0, 10).join(QLatin1Char('\n')));
    return true;
}

bool CommandEngine::cmdOpenWith(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    const QStringList flags = {QStringLiteral("DEFAULT")};
    QStringList files;
    for (const QString &p : sourcesFor(cmd, ctx, flags)) {
        if (!Location::isLocal(p))
            return fail(QStringLiteral("Open With works with files on this computer and its cards for now."));
        files << p;
    }
    if (files.isEmpty())
        return fail(QStringLiteral("Select a file to open first."));
    const QString appName = cmd.value(QStringLiteral("APP"));
    if (appName.isEmpty())
        return ctx.tool(cmd, error); // the chooser
    const AppInfo app = Apps::find(appName);
    if (!app.isValid())
        return fail(QStringLiteral("There is no application called \"%1\".").arg(appName));
    if (cmd.hasFlag(QStringLiteral("DEFAULT")))
        Apps::setDefaultFor(Apps::mimeTypeOf(files.first()), app.id);
    return Apps::open(app, files, error);
}

QList<ScriptRunner *> CommandEngine::runningScripts() const
{
    return findChildren<ScriptRunner *>(QString(), Qt::FindDirectChildrenOnly);
}

bool CommandEngine::runScript(const QString &script, CommandContext &ctx)
{
    // In the source folder when it is on this computer, else the home folder.
    const QString source = ctx.sourcePath();
    const QString dir = Location::isLocal(source) && QFileInfo(source).isDir() ? source : QDir::homePath();
    QStringList args;
    for (const QString &p : ctx.selectedPaths())
        args << p;
    auto *runner = new ScriptRunner(this);
    QObject *owner = ctx.contextObject();
    QPointer<QObject> alive(owner);
    CommandContext *target = &ctx;
    connect(runner, &ScriptRunner::finished, this, [runner, alive, owner, target](const ScriptResult &r, const ScriptOptions &o) {
        if (owner && alive)
            target->scriptFinished(r, o);
        runner->deleteLater();
    });
    QString error;
    if (!runner->start(script, args, dir, source, ctx.destPath(), ctx.focusedPath(), &error)) {
        ctx.showError(QStringLiteral("Script"), error);
        return false;
    }
    return true;
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
            *error = kCanceled;
        return false;
    }

    QString dest;
    const bool ask = cmd.value(QStringLiteral("TO")).compare(QLatin1String("ask"), Qt::CaseInsensitive) == 0;
    if (cmd.hasArg(QStringLiteral("TO")) && !ask)
        dest = resolvePath(cmd.value(QStringLiteral("TO")), ctx.sourcePath());
    else if (!ask)
        dest = ctx.destPath();
    if (dest.isEmpty())
        dest = ctx.chooseDestination(move ? QStringLiteral("Move to") : QStringLiteral("Copy to"));
    if (dest.isEmpty()) {
        if (error)
            *error = kCanceled;
        return false;
    }
    if (Collections::isCollectionLocation(dest)) {
        // Into a collection: a link to each item, nothing is copied.
        const QString name = Collections::nameOf(dest);
        if (name.isEmpty()) {
            if (error)
                *error = QStringLiteral("Open a collection first; this is the list of collections.");
            return false;
        }
        Collections::instance()->add(name, sources);
        return true;
    }
    if (Location::isLocal(dest) && !QFileInfo(dest).isDir()) {
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
            QString name = Location::fileName(src);
            const QString label = QStringLiteral("%1 \"%2\" as:").arg(move ? QStringLiteral("Move") : QStringLiteral("Copy"), name);
            if (!ctx.promptText(move ? QStringLiteral("Move As") : QStringLiteral("Copy As"), label, name)) {
                if (error)
                    *error = kCanceled;
                return false;
            }
            QString why;
            if (!FileOps::isValidFileName(name.trimmed(), &why)) {
                if (error)
                    *error = why;
                return false;
            }
            newNames.insert(Location::clean(src), name.trimmed());
        }
    }

    // Picked in a flat view: items from folders inside it keep those folders
    // or all land in the destination (STRUCTURE=keep|flat, else ask).
    const QString structure = cmd.value(QStringLiteral("STRUCTURE")).toLower();
    if (newNames.isEmpty() && fromSubfolders(sources, ctx.sourcePath())) {
        if (structure == QLatin1String("keep") || structure == QLatin1String("flat") || structure == QLatin1String("same"))
            return copyItemsFrom(sources, ctx.sourcePath(), dest, move, structure == QLatin1String("keep"), whenExists, error);
        if (!structure.isEmpty() && structure != QLatin1String("ask")) {
            if (error)
                *error = QStringLiteral("STRUCTURE takes keep, flat or ask.");
            return false;
        }
        return copyFromFlat(sources, ctx.sourcePath(), dest, move, whenExists, ctx, error);
    }
    copyItems(sources, dest, move, whenExists, newNames);
    return true;
}

bool CommandEngine::cmdDelete(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    const QStringList sources = sourcesFor(cmd, ctx, kDeleteFlags);
    if (sources.isEmpty()) {
        if (error)
            *error = kCanceled;
        return false;
    }
    if (Collections::isCollectionLocation(ctx.sourcePath()) && !cmd.hasArg(QStringLiteral("FILE"))) {
        Collections *c = Collections::instance();
        const QString name = Collections::nameOf(ctx.sourcePath());
        if (name.isEmpty()) {
            // Whole collections; the files in them stay where they are.
            if (!cmd.hasFlag(QStringLiteral("QUIET"))
                && !ctx.confirm(QStringLiteral("Delete collections"),
                                QStringLiteral("Delete %1 collection(s)? The files in them are not touched.").arg(sources.size()))) {
                if (error)
                    *error = kCanceled;
                return false;
            }
            for (const QString &s : sources)
                c->remove(Collections::nameOf(s));
            return true;
        }
        // In a collection, Delete takes items out of it; the files stay.
        c->removeItems(name, sources);
        return true;
    }
    bool anyRemote = false;
    for (const QString &s : sources)
        anyRemote = anyRemote || Location::isRemote(s);
    // Servers and archives have no trash: deleting there is always permanent.
    const bool permanent = cmd.hasFlag(QStringLiteral("NORECYCLE")) || anyRemote;
    if (permanent) {
        if (!cmd.hasFlag(QStringLiteral("QUIET"))) {
            const QString what = sources.size() == 1 ? QStringLiteral("\"%1\"").arg(Location::fileName(sources.first()))
                                                      : QStringLiteral("these %1 items").arg(sources.size());
            const QString text = anyRemote && !cmd.hasFlag(QStringLiteral("NORECYCLE"))
                ? QStringLiteral("Remote files cannot go to the trash. Permanently delete %1? This cannot be undone.").arg(what)
                : QStringLiteral("Permanently delete %1? This cannot be undone.").arg(what);
            if (!ctx.confirm(QStringLiteral("Delete permanently"), text)) {
                if (error)
                    *error = kCanceled;
                return false;
            }
        }
        deleteItems(sources);
    } else {
        trashItems(sources);
    }
    return true;
}

bool CommandEngine::parsePieceSize(const QString &text, qint64 *out)
{
    QString t = text.trimmed().toLower().remove(QLatin1Char(' '));
    if (t == QLatin1String("fat32") || t == QLatin1String("fat")) {
        *out = Q_INT64_C(4294967295); // the largest file FAT32 holds
        return true;
    }
    if (t == QLatin1String("cd")) {
        *out = Q_INT64_C(650) * 1024 * 1024;
        return true;
    }
    if (t == QLatin1String("dvd")) {
        *out = Q_INT64_C(4480) * 1024 * 1024;
        return true;
    }
    for (const char *suffix : {"ib", "b"}) {
        if (t.endsWith(QLatin1String(suffix)) && t.size() > int(qstrlen(suffix))) {
            t.chop(int(qstrlen(suffix)));
            break;
        }
    }
    qint64 mult = 1;
    if (!t.isEmpty()) {
        switch (t.back().toLatin1()) {
        case 'k': mult = 1024; break;
        case 'm': mult = 1024 * 1024; break;
        case 'g': mult = Q_INT64_C(1024) * 1024 * 1024; break;
        case 't': mult = Q_INT64_C(1024) * 1024 * 1024 * 1024; break;
        default: break;
        }
        if (mult > 1)
            t.chop(1);
    }
    bool ok = false;
    const double v = t.toDouble(&ok);
    if (!ok || v <= 0)
        return false;
    *out = qint64(v * double(mult));
    return *out > 0;
}

QStringList CommandEngine::piecesFrom(const QStringList &picked)
{
    static const QRegularExpression numbered(QStringLiteral("^(.*)\\.(\\d{3,})$"));
    if (picked.size() == 1) {
        // One piece picked: the rest of the set, in order, from the first.
        const auto m = numbered.match(picked.first());
        if (!m.hasMatch())
            return picked;
        const QString stem = m.captured(1);
        const int digits = int(m.captured(2).size());
        QStringList out;
        for (int i = 1;; ++i) {
            const QString p = stem + QLatin1Char('.') + QStringLiteral("%1").arg(i, digits, 10, QLatin1Char('0'));
            if (!QFileInfo(p).isFile())
                break;
            out << p;
        }
        return out.isEmpty() ? picked : out;
    }
    QStringList out = picked;
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) { return FileOps::naturalCompare(a, b) < 0; });
    return out;
}

bool CommandEngine::cmdSplitJoin(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    const bool split = cmd.is(QStringLiteral("Split"));
    QStringList sources = sourcesFor(cmd, ctx, {QStringLiteral("DEST")});
    QStringList files;
    for (const QString &p : std::as_const(sources)) {
        if (QFileInfo(p).isFile())
            files << p;
    }
    if (files.isEmpty())
        return fail(split ? QStringLiteral("Select a file to split first.") : QStringLiteral("Select the pieces to join first (the first one is enough)."));
    QString dest;
    if (cmd.hasArg(QStringLiteral("TO")) && !cmd.value(QStringLiteral("TO")).isEmpty()) {
        dest = cmd.value(QStringLiteral("TO")).compare(QLatin1String("dest"), Qt::CaseInsensitive) == 0
            ? ctx.destPath()
            : resolvePath(cmd.value(QStringLiteral("TO")), ctx.sourcePath());
    } else if (cmd.hasFlag(QStringLiteral("DEST"))) {
        dest = ctx.destPath();
    } else {
        dest = Location::parent(files.first());
    }
    if (dest.isEmpty())
        return fail(QStringLiteral("There is no destination: turn on the second display or give TO=."));
    for (const QString &p : std::as_const(files)) {
        if (!Location::isLocal(p))
            return fail(QStringLiteral("Splitting and joining work in folders on this computer and its cards."));
    }
    if (!Location::isLocal(dest) || !QFileInfo(dest).isDir())
        return fail(QStringLiteral("%1 is not a folder on this computer.").arg(dest));
    CollisionAction whenExists = CollisionAction::Rename;
    if (cmd.hasArg(QStringLiteral("WHENEXISTS")) && !parseWhenExists(cmd.value(QStringLiteral("WHENEXISTS")), whenExists))
        return fail(QStringLiteral("WHENEXISTS takes replace, skip or rename."));
    JobSpec spec;
    spec.destDir = dest;
    spec.whenExists = whenExists;
    if (split) {
        QString size = cmd.value(QStringLiteral("SIZE"));
        qint64 bytes = 0;
        if (cmd.hasArg(QStringLiteral("PIECES"))) {
            bool ok = false;
            const int n = cmd.value(QStringLiteral("PIECES")).toInt(&ok);
            if (!ok || n < 2)
                return fail(QStringLiteral("PIECES takes a number from 2 up."));
            qint64 biggest = 0;
            for (const QString &f : std::as_const(files))
                biggest = qMax(biggest, QFileInfo(f).size());
            bytes = qMax<qint64>(1, (biggest + n - 1) / n);
        } else {
            if (size.isEmpty()) {
                size = QStringLiteral("fat32");
                if (!ctx.promptText(QStringLiteral("Split"),
                                    QStringLiteral("Size of each piece (like 100M or 2G; fat32 for the largest a FAT32 card holds; cd, dvd):"), size)) {
                    if (error)
                        *error = kCanceled;
                    return false;
                }
            }
            if (!parsePieceSize(size, &bytes))
                return fail(QStringLiteral("\"%1\" is not a size: try 100M, 2G or fat32.").arg(size));
        }
        if (bytes < 1024 && !cmd.hasArg(QStringLiteral("PIECES")))
            return fail(QStringLiteral("Pieces need to be at least 1 KB."));
        spec.type = JobType::Split;
        spec.sources = files;
        spec.pieceSize = bytes;
    } else {
        const QStringList pieces = piecesFrom(files);
        if (pieces.size() < 2)
            return fail(QStringLiteral("Joining needs at least two pieces."));
        QString name = cmd.value(QStringLiteral("NAME"));
        if (name.isEmpty()) {
            static const QRegularExpression numbered(QStringLiteral("\\.\\d{3,}$"));
            name = Location::fileName(pieces.first());
            if (numbered.match(name).hasMatch())
                name.remove(numbered);
            else if (!ctx.promptText(QStringLiteral("Join"), QStringLiteral("Name of the joined file:"), name)) {
                if (error)
                    *error = kCanceled;
                return false;
            }
        }
        QString why;
        if (!FileOps::isValidFileName(name, &why))
            return fail(why);
        spec.type = JobType::Join;
        spec.sources = pieces;
        spec.joinName = name;
    }
    m_jobs.enqueue(spec);
    return true;
}

bool CommandEngine::cmdClean(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    // Folders named, else the selected folders, else the source folder.
    QStringList roots;
    for (const QString &f : cmd.values(QStringLiteral("FILE")))
        roots << resolvePath(f, ctx.sourcePath());
    if (roots.isEmpty()) {
        for (const QString &p : ctx.selectedPaths())
            if (QFileInfo(p).isDir() && !FileOps::isJunk(Location::fileName(p), true))
                roots << p;
    }
    if (roots.isEmpty())
        roots << ctx.sourcePath();
    for (const QString &r : std::as_const(roots)) {
        if (!Location::isLocal(r) || !QFileInfo(r).isDir())
            return fail(QStringLiteral("Clean works in folders on this computer and its cards."));
    }
    const QStringList junk = FileOps::findJunk(roots);
    if (junk.isEmpty()) {
        if (!cmd.hasFlag(QStringLiteral("QUIET")))
            ctx.showError(QStringLiteral("Clean"), QStringLiteral("No junk files here."));
        return true;
    }
    if (!cmd.hasFlag(QStringLiteral("QUIET"))) {
        QStringList names;
        for (const QString &j : junk.mid(0, 12))
            names << QStringLiteral("  ") + j.mid(roots.size() == 1 ? roots.first().size() + 1 : 0);
        if (junk.size() > 12)
            names << QStringLiteral("  ... and %1 more").arg(junk.size() - 12);
        if (!ctx.confirm(QStringLiteral("Clean"),
                         QStringLiteral("Move %1 junk item(s) to the trash?\n\n%2").arg(junk.size()).arg(names.join(QLatin1Char('\n'))))) {
            if (error)
                *error = kCanceled;
            return false;
        }
    }
    trashItems(junk);
    return true;
}

bool CommandEngine::cmdRename(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (cmd.hasFlag(QStringLiteral("UNDO"))) {
        const UndoEntry *last = m_history.latestRename();
        if (!last)
            return fail(QStringLiteral("There is no rename to undo."));
        QStringList back;
        QString err;
        if (!m_history.undo(last->id, &back, &err))
            return fail(err);
        ctx.afterLocalChange(back);
        return true;
    }
    const bool advanced = cmd.hasFlag(QStringLiteral("ADVANCED"));
    const bool scripted = cmd.hasArg(QStringLiteral("OLDNAME")) || cmd.hasArg(QStringLiteral("NEWNAME"))
        || cmd.hasArg(QStringLiteral("PRESET"));
    if (advanced || scripted) {
        RenameOptions o;
        if (cmd.hasArg(QStringLiteral("PRESET"))
            && !RenamePresets::load(cmd.value(QStringLiteral("PRESET")), o))
            return fail(QStringLiteral("There is no rename preset called \"%1\".").arg(cmd.value(QStringLiteral("PRESET"))));
        if (cmd.hasArg(QStringLiteral("MODE")) && !RenameOptions::modeFromName(cmd.value(QStringLiteral("MODE")), o.mode))
            return fail(QStringLiteral("MODE takes wildcard, findreplace or regexp."));
        if (cmd.hasArg(QStringLiteral("OLDNAME")))
            o.oldPattern = cmd.value(QStringLiteral("OLDNAME"));
        if (cmd.hasArg(QStringLiteral("NEWNAME")))
            o.newPattern = cmd.value(QStringLiteral("NEWNAME"));
        if (cmd.hasArg(QStringLiteral("CASE")) && !RenameOptions::caseFromName(cmd.value(QStringLiteral("CASE")), o.nameCase))
            return fail(QStringLiteral("CASE takes lower, upper, title or sentence."));
        if (cmd.hasArg(QStringLiteral("EXTCASE"))) {
            const QString e = cmd.value(QStringLiteral("EXTCASE")).toLower();
            o.extCase = e == QLatin1String("lower") ? RenameOptions::ExtCase::Lower
                : e == QLatin1String("upper")       ? RenameOptions::ExtCase::Upper
                                                    : RenameOptions::ExtCase::Keep;
        }
        if (cmd.hasFlag(QStringLiteral("IGNOREEXT")))
            o.ignoreExtension = true;
        if (cmd.hasFlag(QStringLiteral("CASESENSITIVE")))
            o.caseSensitive = true;
        if (cmd.hasFlag(QStringLiteral("TRIM")))
            o.trimSpaces = true;
        if (cmd.hasArg(QStringLiteral("START")))
            o.counterStart = cmd.value(QStringLiteral("START")).toInt();
        if (cmd.hasArg(QStringLiteral("STEP")))
            o.counterStep = cmd.value(QStringLiteral("STEP")).toInt();
        if (cmd.hasArg(QStringLiteral("WIDTH")))
            o.counterWidth = cmd.value(QStringLiteral("WIDTH")).toInt();

        QStringList paths;
        for (const QString &f : cmd.values(QStringLiteral("FILE")))
            paths << resolvePath(f, ctx.sourcePath());
        if (paths.isEmpty())
            paths = ctx.selectedPaths();
        if (paths.isEmpty())
            return fail(QStringLiteral("Select what to rename first."));
        if (advanced)
            return ctx.advancedRename(paths, o, error);
        const QList<RenameItem> items = RenameEngine::preview(paths, o);
        if (RenameEngine::hasConflicts(items)) {
            QStringList lines;
            for (const RenameItem &it : items) {
                if (it.status == RenameItem::Status::Conflict || it.status == RenameItem::Status::Invalid)
                    lines << QStringLiteral("%1 -> %2: %3").arg(it.oldName, it.newName, it.note);
            }
            return fail(QStringLiteral("Nothing was renamed:\n") + lines.mid(0, 10).join(QLatin1Char('\n')));
        }
        QList<QPair<QString, QString>> done;
        QString err;
        if (!RenameEngine::apply(items, &done, &err))
            return fail(err);
        if (!done.isEmpty())
            recordRename(done);
        QStringList renamed;
        for (const auto &d : std::as_const(done))
            renamed << d.first;
        ctx.afterLocalChange(renamed);
        return true;
    }
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
    if (Collections::isCollectionLocation(from)) {
        // Renaming a collection itself.
        if (!Collections::instance()->rename(Collections::nameOf(from), to, error))
            return false;
        ctx.afterLocalChange({Collections::location(to)});
        return true;
    }
    if (Location::isRemote(from)) {
        QString why;
        if (!FileOps::isValidFileName(to, &why)) {
            if (error)
                *error = why;
            return false;
        }
        RemoteBackend *backend = RemoteBackend::instance();
        if (!backend) {
            if (error)
                *error = QStringLiteral("Remote locations need the KIO build of Linopus.");
            return false;
        }
        newPath = Location::join(Location::parent(from), to);
        if (!backend->rename(from, newPath, error))
            return false;
    } else if (!FileOps::renameEntry(from, to, &newPath, error)) {
        return false;
    } else {
        recordRename({qMakePair(newPath, from)}); // also tells collections
        ctx.afterLocalChange({newPath});
        return true;
    }
    Collections::instance()->renamed(from, newPath);
    FileTags::renamed(from, newPath);
    ctx.afterLocalChange({newPath});
    return true;
}

bool CommandEngine::cmdCreateFolder(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    const QString parent = cmd.hasArg(QStringLiteral("IN")) ? resolvePath(cmd.value(QStringLiteral("IN")), ctx.sourcePath())
                                                           : ctx.sourcePath();
    if (Collections::isCollectionLocation(parent)) {
        if (!Collections::nameOf(parent).isEmpty()) {
            if (error)
                *error = QStringLiteral("A collection holds links, not folders.");
            return false;
        }
        QString names = cmd.values(QStringLiteral("NAME")).join(QLatin1Char(','));
        if (names.trimmed().isEmpty()) {
            QString value = QStringLiteral("New Collection");
            if (!ctx.promptText(QStringLiteral("New Collection"), QStringLiteral("Collection name:"), value)) {
                if (error)
                    *error = kCanceled;
                return false;
            }
            names = value;
        }
        QStringList made;
        for (const QString &n : names.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            QString why;
            if (!Collections::instance()->create(n.trimmed(), &why)) {
                if (error)
                    *error = why;
                return false;
            }
            made << Collections::location(n.trimmed());
        }
        ctx.afterLocalChange(made);
        return true;
    }
    QString names = cmd.values(QStringLiteral("NAME")).join(QLatin1Char(','));
    if (names.isEmpty())
        names = cmd.positional.join(QLatin1Char(' '));
    if (names.trimmed().isEmpty()) {
        QString value = QStringLiteral("New Folder");
        if (!ctx.promptText(QStringLiteral("Create Folder"),
                            QStringLiteral("Folder name (separate several with commas):"), value)) {
            if (error)
                *error = kCanceled;
            return false;
        }
        names = value;
    }
    QString problems;
    QStringList created;
    if (Location::isRemote(parent)) {
        RemoteBackend *backend = RemoteBackend::instance();
        QStringList issues;
        for (QString name : names.split(QLatin1Char(','))) {
            name = name.trimmed();
            if (name.isEmpty())
                continue;
            QString why;
            if (!FileOps::isValidFileName(name, &why)) {
                issues << QStringLiteral("%1: %2").arg(name, why);
                continue;
            }
            const QString full = Location::join(parent, name);
            if (!backend) {
                issues << QStringLiteral("Remote locations need the KIO build of Linopus.");
                break;
            }
            if (backend->mkdir(full, &why))
                created << full;
            else
                issues << QStringLiteral("%1: %2").arg(name, why);
        }
        problems = issues.join(QLatin1Char('\n'));
    } else {
        created = FileOps::createFolders(parent, names, &problems);
        if (!created.isEmpty())
            recordNew(created, QStringLiteral("folder"));
    }
    if (!created.isEmpty())
        ctx.afterLocalChange(created);
    if (!problems.isEmpty()) {
        if (error)
            *error = problems;
        return false;
    }
    return true;
}

bool CommandEngine::pasteAsFile(const QMimeData *mime, bool askName, CommandContext &ctx, QString *error)
{
    // A picture or text on the clipboard becomes a file in the source, as in
    // Opus: screenshots straight into a folder.
    const bool image = mime->hasImage();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HHmmss"));
    QString name = image ? QStringLiteral("Pasted image %1.png").arg(stamp) : QStringLiteral("Pasted text %1.txt").arg(stamp);
    if (askName) {
        if (!ctx.promptText(QStringLiteral("Paste as File"), QStringLiteral("File name:"), name)) {
            if (error)
                *error = kCanceled;
            return false;
        }
        name = name.trimmed();
        if (name.isEmpty() || name.contains(QLatin1Char('/'))) {
            if (error)
                *error = QStringLiteral("That is not a usable file name.");
            return false;
        }
        if (image && QFileInfo(name).suffix().isEmpty())
            name += QStringLiteral(".png");
    }
    QByteArray data;
    QString format;
    if (image) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        format = QFileInfo(name).suffix().toUpper();
        if (format == QLatin1String("JPEG"))
            format = QStringLiteral("JPG");
        if (format != QLatin1String("JPG") && format != QLatin1String("PNG") && format != QLatin1String("WEBP")
            && format != QLatin1String("BMP"))
            format = QStringLiteral("PNG");
        QBuffer buf(&data);
        buf.open(QIODevice::WriteOnly);
        if (img.isNull() || !img.save(&buf, format.toLatin1().constData(), format == QLatin1String("JPG") ? 92 : -1)) {
            if (error)
                *error = QStringLiteral("The picture on the clipboard could not be saved.");
            return false;
        }
    } else {
        data = mime->text().toUtf8();
    }

    const QString dir = ctx.sourcePath();
    if (Collections::isCollectionLocation(dir)) {
        if (error)
            *error = QStringLiteral("A collection holds links to files; paste the picture into a folder.");
        return false;
    }
    if (Location::isLocal(dir)) {
        // Never overwrite: "name (2).png" when the name is taken.
        QString target = Location::join(dir, name);
        const QFileInfo fi(name);
        for (int n = 2; QFileInfo::exists(target); ++n)
            target = Location::join(dir, QStringLiteral("%1 (%2).%3").arg(fi.completeBaseName()).arg(n).arg(fi.suffix()));
        QSaveFile f(target);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
            if (error)
                *error = QStringLiteral("Could not write %1: %2").arg(target, f.errorString());
            return false;
        }
        recordNew({target}, QStringLiteral("file"));
        ctx.afterLocalChange({target});
        return true;
    }
    // A server: write it locally under its name, then upload it like any copy.
    const QString tmpDir = QDir(QDir::tempPath()).filePath(QStringLiteral("linopus-paste-%1").arg(QDateTime::currentMSecsSinceEpoch()));
    QDir().mkpath(tmpDir);
    const QString tmp = QDir(tmpDir).filePath(name);
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
        if (error)
            *error = QStringLiteral("Could not write a temporary file.");
        return false;
    }
    f.close();
    copyItems({tmp}, dir, true, CollisionAction::Rename);
    return true;
}

bool CommandEngine::cmdCollection(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    Collections *c = Collections::instance();
    if (cmd.hasArg(QStringLiteral("ADD"))) {
        QString name = cmd.value(QStringLiteral("ADD"));
        if (name.isEmpty() || name == QLatin1String("ask")) {
            name = QStringLiteral("Favorites");
            if (!ctx.promptText(QStringLiteral("Add to Collection"), QStringLiteral("Collection (new or existing):"), name))
                return fail(kCanceled);
        }
        const QStringList paths = sourcesFor(cmd, ctx, {QStringLiteral("ADD")});
        if (paths.isEmpty())
            return fail(QStringLiteral("Select what to add first."));
        QString why;
        if (!c->exists(name) && !c->create(name, &why))
            return fail(why);
        c->add(name, paths);
        return true;
    }
    if (cmd.hasFlag(QStringLiteral("REMOVE"))) {
        const QString name = Collections::nameOf(ctx.sourcePath());
        if (name.isEmpty())
            return fail(QStringLiteral("Open a collection first."));
        c->removeItems(name, ctx.selectedPaths());
        return true;
    }
    if (cmd.hasArg(QStringLiteral("CREATE"))) {
        QString why;
        if (!c->create(cmd.value(QStringLiteral("CREATE")), &why))
            return fail(why);
        return true;
    }
    if (cmd.hasArg(QStringLiteral("DELETE"))) {
        if (!c->remove(cmd.value(QStringLiteral("DELETE"))))
            return fail(QStringLiteral("There is no collection called %1.").arg(cmd.value(QStringLiteral("DELETE"))));
        return true;
    }
    if (cmd.hasArg(QStringLiteral("OPEN")) || cmd.positional.isEmpty() || cmd.hasFlag(QStringLiteral("NEWTAB"))) {
        const QString name = cmd.value(QStringLiteral("OPEN"));
        ParsedCommand go;
        go.name = QStringLiteral("Go");
        go.args[QStringLiteral("PATH")] = QStringList{name.isEmpty() ? QString::fromLatin1(Collections::kRoot) : Collections::location(name)};
        if (cmd.hasFlag(QStringLiteral("NEWTAB")))
            go.positional << QStringLiteral("NEWTAB");
        return ctx.go(go, error);
    }
    return fail(QStringLiteral("Collection takes ADD=, REMOVE, CREATE=, DELETE= or OPEN=."));
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
                *error = kCanceled;
            return false;
        }
        auto *mime = new QMimeData;
        const QList<QUrl> urls = Location::toUrls(paths);
        mime->setUrls(urls);
        // KDE and GNOME each mark a cut their own way; set both.
        mime->setData(QStringLiteral("application/x-kde-cutselection"), cut ? "1" : "0");
        QByteArray gnome = cut ? "cut" : "copy";
        for (const QUrl &u : urls)
            gnome += '\n' + u.toEncoded();
        mime->setData(QStringLiteral("x-special/gnome-copied-files"), gnome);
        // Where they were picked: a flat view's items from folders further
        // down ask at paste time whether to keep those folders.
        mime->setData(QStringLiteral("application/x-linopus-source-folder"), ctx.sourcePath().toUtf8());
        clipboard->setMimeData(mime);
        return true;
    }
    if (cmd.hasFlag(QStringLiteral("PASTE"))) {
        const QMimeData *mime = clipboard->mimeData();
        if (mime && !mime->hasUrls() && (mime->hasImage() || mime->hasText()))
            return pasteAsFile(mime, cmd.hasFlag(QStringLiteral("AS")), ctx, error);
        if (!mime || !mime->hasUrls()) {
            if (error)
                *error = QStringLiteral("The clipboard holds no files.");
            return false;
        }
        QStringList sources;
        for (const QUrl &u : mime->urls()) {
            if (u.isValid() && !u.scheme().isEmpty())
                sources << Location::fromUrl(u);
        }
        if (sources.isEmpty()) {
            if (error)
                *error = QStringLiteral("The clipboard holds no files.");
            return false;
        }
        if (Collections::isCollectionLocation(ctx.sourcePath())) {
            const QString name = Collections::nameOf(ctx.sourcePath());
            if (name.isEmpty()) {
                if (error)
                    *error = QStringLiteral("Open a collection to paste into it.");
                return false;
            }
            Collections::instance()->add(name, sources);
            return true;
        }
        bool isCut = mime->data(QStringLiteral("application/x-kde-cutselection")) == "1";
        if (!isCut) {
            const QByteArray g = mime->data(QStringLiteral("x-special/gnome-copied-files"));
            isCut = g.startsWith("cut");
        }
        const QString root = QString::fromUtf8(mime->data(QStringLiteral("application/x-linopus-source-folder")));
        if (fromSubfolders(sources, root)) {
            const QString structure = cmd.value(QStringLiteral("STRUCTURE")).toLower();
            bool ok;
            if (structure == QLatin1String("keep") || structure == QLatin1String("flat") || structure == QLatin1String("same"))
                ok = copyItemsFrom(sources, root, ctx.sourcePath(), isCut, structure == QLatin1String("keep"),
                                   CollisionAction::Ask, error);
            else
                ok = copyFromFlat(sources, root, ctx.sourcePath(), isCut, CollisionAction::Ask, ctx, error);
            if (!ok)
                return false;
        } else {
            copyItems(sources, ctx.sourcePath(), isCut);
        }
        if (isCut)
            clipboard->clear(); // a cut pastes once
        return true;
    }
    if (error)
        *error = QStringLiteral("Clipboard takes COPY, CUT or PASTE.");
    return false;
}

bool CommandEngine::cmdUndo(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    if (cmd.hasFlag(QStringLiteral("LIST")) || cmd.hasFlag(QStringLiteral("LOG")))
        return ctx.tool(cmd, error); // the History window
    int id = 0;
    if (cmd.hasArg(QStringLiteral("ID"))) {
        id = cmd.value(QStringLiteral("ID")).toInt();
    } else if (const UndoEntry *last = m_history.latest()) {
        id = last->id;
    }
    if (id <= 0) {
        if (error)
            *error = QStringLiteral("There is nothing to undo.");
        return false;
    }
    QStringList touched;
    QString problems;
    const bool ok = m_history.undo(id, &touched, &problems);
    if (!touched.isEmpty())
        ctx.afterLocalChange(touched);
    if (!ok) {
        if (error)
            *error = problems.isEmpty() ? QStringLiteral("Nothing could be undone.") : problems;
        return false;
    }
    if (!problems.isEmpty())
        ctx.showError(QStringLiteral("Undo"), problems); // done, with exceptions
    return true;
}

int CommandEngine::applyTags(const QList<QPair<QString, MediaEdit>> &edits, const QString &title, QStringList *problems)
{
    UndoEntry undo;
    undo.kind = UndoEntry::Kind::Tags;
    undo.title = title;
    int written = 0;
    for (const auto &[path, e] : edits) {
        if (e.isEmpty())
            continue;
        const MediaEdit before = e.currentValues(Metadata::read(path));
        QString why;
        if (!Metadata::write(path, e, &why)) {
            if (problems)
                *problems << QStringLiteral("%1: %2").arg(Location::fileName(path), why);
            continue;
        }
        undo.items << qMakePair(path, path);
        undo.before << before.toMap();
        ++written;
    }
    m_history.add(undo); // also the file log (nothing when nothing was written)
    return written;
}

bool CommandEngine::cmdMetadata(const ParsedCommand &cmd, CommandContext &ctx, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (cmd.hasArg(QStringLiteral("PANE")))
        return ctx.tool(cmd, error); // the Metadata pane belongs to the lister

    QStringList paths;
    for (const QString &f : cmd.values(QStringLiteral("FILE")))
        paths << resolvePath(f, ctx.sourcePath());
    if (paths.isEmpty())
        paths = ctx.selectedPaths();

    if (cmd.hasFlag(QStringLiteral("COPY"))) {
        const QString from = paths.isEmpty() ? ctx.focusedPath() : paths.first();
        if (from.isEmpty() || Metadata::kindFor(from) == MediaInfo::Kind::None || !Location::isLocal(from))
            return fail(QStringLiteral("Select a music file or a photo to copy its tags."));
        m_copiedTags = Metadata::read(from);
        return true;
    }

    QStringList targets;
    QStringList skipped;
    for (const QString &p : std::as_const(paths)) {
        if (Location::isLocal(p) && Metadata::canWrite(p))
            targets << p;
        else
            skipped << Location::fileName(p);
    }
    if (targets.isEmpty())
        return fail(paths.isEmpty() ? QStringLiteral("Select music files or photos first.")
                                    : QStringLiteral("Tags cannot be written to these files (MP3, FLAC, Ogg, M4A, WAV, JPEG, PNG, WebP and TIFF can)."));

    QList<QPair<QString, MediaEdit>> edits;
    QString title;
    if (cmd.hasFlag(QStringLiteral("PASTE"))) {
        if (!m_copiedTags)
            return fail(QStringLiteral("Copy tags from a file first (Metadata COPY)."));
        const MediaInfo &c = *m_copiedTags;
        const bool all = cmd.hasFlag(QStringLiteral("ALL"));
        MediaEdit e;
        // The fields episodes or a shoot share; titles and track numbers only with ALL.
        e.artist = c.artist;
        e.albumArtist = c.albumArtist;
        e.album = c.album;
        e.genre = c.genre;
        e.year = c.year;
        e.comment = c.comment;
        e.copyright = c.copyright;
        if (all) {
            e.title = c.title;
            e.track = c.track;
            e.disc = c.disc;
            e.description = c.description;
            e.taken = c.taken;
        }
        for (const QString &p : std::as_const(targets))
            edits << qMakePair(p, e);
        title = QStringLiteral("Paste tags onto %1").arg(targets.size() == 1 ? Location::fileName(targets.first())
                                                                               : QStringLiteral("%1 files").arg(targets.size()));
    } else {
        MediaEdit e;
        auto str = [&cmd](const char *key, std::optional<QString> &out) {
            if (cmd.hasArg(QLatin1String(key)))
                out = cmd.value(QLatin1String(key));
        };
        str("TITLE", e.title);
        str("ARTIST", e.artist);
        str("ALBUMARTIST", e.albumArtist);
        str("ALBUM", e.album);
        str("GENRE", e.genre);
        str("COMMENT", e.comment);
        str("DESCRIPTION", e.description);
        str("COPYRIGHT", e.copyright);
        auto num = [&cmd](const char *key, std::optional<int> &out) {
            if (!cmd.hasArg(QLatin1String(key)))
                return true;
            const QString v = cmd.value(QLatin1String(key));
            if (v.isEmpty()) {
                out = 0;
                return true;
            }
            bool ok = false;
            out = v.toInt(&ok);
            return ok && *out >= 0;
        };
        const bool autoTrack = cmd.value(QStringLiteral("TRACK")).compare(QLatin1String("auto"), Qt::CaseInsensitive) == 0;
        if (!num("YEAR", e.year) || (!autoTrack && !num("TRACK", e.track)) || !num("DISC", e.disc))
            return fail(QStringLiteral("YEAR, TRACK and DISC take a number (TRACK=auto numbers the files in list order)."));
        if (cmd.hasArg(QStringLiteral("TAKEN"))) {
            const QString v = cmd.value(QStringLiteral("TAKEN"));
            QDateTime when;
            for (const QString &fmt : {QStringLiteral("yyyy-MM-dd HH:mm:ss"), QStringLiteral("yyyy-MM-dd HH:mm"),
                                       QStringLiteral("yyyy-MM-dd"), QStringLiteral("yyyy:MM:dd HH:mm:ss")}) {
                when = QDateTime::fromString(v, fmt);
                if (when.isValid())
                    break;
            }
            if (!when.isValid() && !v.isEmpty())
                return fail(QStringLiteral("TAKEN takes a date like 2026-10-21 18:15 (empty clears it)."));
            e.taken = when;
        }
        qint64 shift = 0;
        if (cmd.hasArg(QStringLiteral("SHIFT")) && !Metadata::parseShift(cmd.value(QStringLiteral("SHIFT")), &shift))
            return fail(QStringLiteral("SHIFT takes +1h, -30m, +2d, +90s or -1:30."));
        int number = cmd.hasArg(QStringLiteral("START")) ? cmd.value(QStringLiteral("START")).toInt() : 1;
        for (const QString &p : std::as_const(targets)) {
            MediaEdit one = e;
            if (autoTrack)
                one.track = number++;
            if (shift != 0) {
                // A camera's clock was off: move each photo's own date.
                const QDateTime was = Metadata::read(p).taken;
                if (!was.isValid()) {
                    skipped << Location::fileName(p);
                    continue;
                }
                one.taken = was.addSecs(shift);
            }
            if (!one.isEmpty())
                edits << qMakePair(p, one);
        }
        if (edits.isEmpty())
            return fail(QStringLiteral("Nothing to change: give fields like ARTIST= or ALBUM=, TRACK=auto, or SHIFT=."));
        const QStringList fields = edits.first().second.fieldNames();
        title = QStringLiteral("Set %1 on %2").arg(fields.join(QStringLiteral(", ")),
                                                    edits.size() == 1 ? Location::fileName(edits.first().first)
                                                                      : QStringLiteral("%1 files").arg(edits.size()));
    }
    QStringList problems;
    const int written = applyTags(edits, title, &problems);
    QStringList changed;
    for (const auto &e : std::as_const(edits))
        changed << e.first;
    ctx.afterLocalChange(changed);
    if (written == 0)
        return fail(problems.isEmpty() ? QStringLiteral("Nothing was written.") : problems.join(QLatin1Char('\n')));
    if (!problems.isEmpty())
        ctx.showError(QStringLiteral("Metadata"), QStringLiteral("Written to %1; not to these:\n%2").arg(written).arg(problems.join(QLatin1Char('\n'))));
    return true;
}
