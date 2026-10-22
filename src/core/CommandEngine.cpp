#include "core/CommandEngine.h"

#include "core/AliasManager.h"
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


} // namespace

CommandEngine::CommandEngine(AliasManager &aliases, JobManager &jobs, QObject *parent)
    : QObject(parent)
    , m_aliases(aliases)
    , m_jobs(jobs)
{
}

void CommandEngine::recordRename(const QList<QPair<QString, QString>> &done)
{
    m_lastRename = done;
    for (const auto &d : done)
        Collections::instance()->renamed(d.second, d.first);
}

QStringList CommandEngine::commandNames()
{
    return {QStringLiteral("Clipboard"), QStringLiteral("Collection"), QStringLiteral("Copy"), QStringLiteral("CreateFolder"),
            QStringLiteral("Delete"), QStringLiteral("Duplicates"), QStringLiteral("Favorite"), QStringLiteral("Find"), QStringLiteral("Go"), QStringLiteral("Move"), QStringLiteral("Print"),
            QStringLiteral("Rename"), QStringLiteral("Select"), QStringLiteral("Set"), QStringLiteral("Settings"),
            QStringLiteral("Sync"), QStringLiteral("Toolbar")};
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
    if (cmd.is(QStringLiteral("Sync")) || cmd.is(QStringLiteral("Duplicates")) || cmd.is(QStringLiteral("Find"))
        || cmd.is(QStringLiteral("Favorite")))
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
                    *error = kCancelled;
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
    if (Collections::isCollectionLocation(ctx.sourcePath()) && !cmd.hasArg(QStringLiteral("FILE"))) {
        Collections *c = Collections::instance();
        const QString name = Collections::nameOf(ctx.sourcePath());
        if (name.isEmpty()) {
            // Whole collections; the files in them stay where they are.
            if (!cmd.hasFlag(QStringLiteral("QUIET"))
                && !ctx.confirm(QStringLiteral("Delete collections"),
                                QStringLiteral("Delete %1 collection(s)? The files in them are not touched.").arg(sources.size()))) {
                if (error)
                    *error = kCancelled;
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
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (cmd.hasFlag(QStringLiteral("UNDO"))) {
        if (m_lastRename.isEmpty())
            return fail(QStringLiteral("There is no rename to undo."));
        QString err;
        if (!RenameEngine::undo(m_lastRename, &err))
            return fail(err);
        QStringList back;
        for (const auto &d : std::as_const(m_lastRename)) {
            back << d.second;
            Collections::instance()->renamed(d.first, d.second);
        }
        m_lastRename.clear();
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
            m_lastRename = done;
        for (const auto &d : std::as_const(done))
            Collections::instance()->renamed(d.second, d.first);
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
    }
    Collections::instance()->renamed(from, newPath);
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
                    *error = kCancelled;
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
                *error = kCancelled;
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
                *error = kCancelled;
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
            name = QStringLiteral("Favourites");
            if (!ctx.promptText(QStringLiteral("Add to Collection"), QStringLiteral("Collection (new or existing):"), name))
                return fail(kCancelled);
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
                *error = kCancelled;
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
        copyItems(sources, ctx.sourcePath(), isCut);
        if (isCut)
            clipboard->clear(); // a cut pastes once
        return true;
    }
    if (error)
        *error = QStringLiteral("Clipboard takes COPY, CUT or PASTE.");
    return false;
}
