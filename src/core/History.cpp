#include "core/History.h"

#include "core/Collections.h"
#include "core/FileOps.h"
#include "core/JobManager.h"
#include "core/Location.h"
#include "core/Paths.h"
#include "core/RenameEngine.h"
#include "core/Trash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <cstdio>

// ---- File log ---------------------------------------------------------------

namespace FileLog {

namespace {

QString clean(QString s)
{
    s.replace(QLatin1Char('\t'), QLatin1Char(' '));
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return s;
}

int g_appendsSinceTrim = 0;

void trim()
{
    QFile f(file());
    if (!f.open(QIODevice::ReadOnly))
        return;
    QList<QByteArray> lines = f.readAll().split('\n');
    f.close();
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    if (lines.size() <= kKeep)
        return;
    lines = lines.mid(lines.size() - kKeep);
    QSaveFile out(file());
    if (!out.open(QIODevice::WriteOnly))
        return;
    for (const QByteArray &l : std::as_const(lines)) {
        out.write(l);
        out.write("\n");
    }
    out.commit();
}

} // namespace

QString file()
{
    return Paths::userDataHome() + QStringLiteral("/linopus/file-log.tsv");
}

void append(const QList<Entry> &entries)
{
    if (entries.isEmpty())
        return;
    QDir().mkpath(QFileInfo(file()).absolutePath());
    QFile f(file());
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&f);
    for (const Entry &e : entries) {
        out << (e.when.isValid() ? e.when : QDateTime::currentDateTime()).toString(Qt::ISODate) << '\t' << clean(e.action)
            << '\t' << clean(e.item) << '\t' << clean(e.to) << '\t' << clean(e.note) << '\n';
    }
    f.close();
    // Keep it from growing forever, without rewriting it on every line.
    g_appendsSinceTrim += int(entries.size());
    if (g_appendsSinceTrim > 500) {
        g_appendsSinceTrim = 0;
        trim();
    }
}

void append(const Entry &entry)
{
    append(QList<Entry>{entry});
}

QList<Entry> read()
{
    QList<Entry> out;
    QFile f(file());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QStringList parts = in.readLine().split(QLatin1Char('\t'));
        if (parts.size() < 3)
            continue;
        Entry e;
        e.when = QDateTime::fromString(parts.at(0), Qt::ISODate);
        e.action = parts.at(1);
        e.item = parts.at(2);
        e.to = parts.value(3);
        e.note = parts.value(4);
        out << e;
    }
    return out;
}

void clear()
{
    QFile::remove(file());
}

QString toText(const QList<Entry> &entries)
{
    QString s;
    for (const Entry &e : entries) {
        s += e.when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + QStringLiteral("  ") + e.action + QStringLiteral("  ") + e.item;
        if (!e.to.isEmpty())
            s += QStringLiteral("  ->  ") + e.to;
        if (!e.note.isEmpty())
            s += QStringLiteral("  (") + e.note + QLatin1Char(')');
        s += QLatin1Char('\n');
    }
    return s;
}

} // namespace FileLog

// ---- Undo -------------------------------------------------------------------

UndoHistory::UndoHistory(JobManager &jobs, QObject *parent)
    : QObject(parent)
    , m_jobs(jobs)
{
    connect(&m_jobs, &JobManager::jobFinished, this, &UndoHistory::onJobFinished);
}

QString UndoHistory::kindName(UndoEntry::Kind kind)
{
    switch (kind) {
    case UndoEntry::Kind::Copy: return QStringLiteral("Copy");
    case UndoEntry::Kind::Move: return QStringLiteral("Move");
    case UndoEntry::Kind::Trash: return QStringLiteral("Trash");
    case UndoEntry::Kind::Restore: return QStringLiteral("Restore");
    case UndoEntry::Kind::NewItem: return QStringLiteral("New");
    case UndoEntry::Kind::Rename: return QStringLiteral("Rename");
    }
    return {};
}

const UndoEntry *UndoHistory::latestRename() const
{
    for (const UndoEntry &e : m_entries) {
        if (e.kind == UndoEntry::Kind::Rename && !e.isUndo)
            return &e;
    }
    return nullptr;
}

int UndoHistory::add(UndoEntry entry)
{
    if (entry.items.isEmpty())
        return 0;
    entry.id = m_nextId++;
    if (!entry.when.isValid())
        entry.when = QDateTime::currentDateTime();
    if (entry.title.isEmpty()) {
        const QString what = entry.items.size() == 1 ? Location::fileName(entry.items.first().first)
                                                     : QStringLiteral("%1 items").arg(entry.items.size());
        entry.title = QStringLiteral("%1 %2").arg(kindName(entry.kind), what);
    }
    QList<FileLog::Entry> log;
    const QString action = (entry.isUndo ? QStringLiteral("Undo: ") : QString()) + kindName(entry.kind);
    for (const auto &it : std::as_const(entry.items)) {
        // Renames are kept as (now, before); the log reads before -> now.
        if (entry.kind == UndoEntry::Kind::Rename)
            log << FileLog::Entry{entry.when, action, it.second, it.first, {}};
        else
            log << FileLog::Entry{entry.when, action, it.first, it.second,
                                  entry.replacedTargets.contains(it.second) ? QStringLiteral("replaced a file") : QString()};
    }
    FileLog::append(log);
    m_entries.prepend(entry);
    while (m_entries.size() > kKeep)
        m_entries.removeLast();
    Q_EMIT changed();
    return entry.id;
}

void UndoHistory::onJobFinished(int id)
{
    const JobSpec *spec = m_jobs.spec(id);
    if (!spec)
        return;
    const JobSnapshot snap = m_jobs.snapshot(id);
    const QList<JobOutcome> outcomes = m_jobs.outcomes(id);
    const bool byUndo = m_undoJobs.removeAll(id) > 0;

    // The log gets everything, errors too; undo gets what can be undone.
    QList<FileLog::Entry> log;
    const QString prefix = byUndo ? QStringLiteral("Undo: ") : QString();
    const QString action = spec->type == JobType::Copy   ? QStringLiteral("Copy")
        : spec->type == JobType::Move                    ? QStringLiteral("Move")
        : spec->type == JobType::Trash                   ? QStringLiteral("Trash")
                                                         : QStringLiteral("Delete");
    if (spec->type == JobType::Delete) {
        for (const QString &s : spec->sources)
            log << FileLog::Entry{QDateTime::currentDateTime(), action, s, {}, QStringLiteral("deleted for good")};
    }
    for (const QString &err : snap.errors)
        log << FileLog::Entry{QDateTime::currentDateTime(), prefix + action, err, {}, QStringLiteral("failed")};
    if (outcomes.isEmpty() && spec->type != JobType::Delete) {
        // Remote jobs: no per-item record, so log the sources.
        if (!log.isEmpty() || snap.state != JobState::Cancelled) {
            for (const QString &s : spec->sources)
                log << FileLog::Entry{QDateTime::currentDateTime(), prefix + action, s, spec->destDir,
                                      snap.state == JobState::Cancelled ? QStringLiteral("cancelled") : QString()};
        }
    }
    FileLog::append(log);

    if (outcomes.isEmpty() || spec->type == JobType::Delete)
        return;
    UndoEntry e;
    e.kind = spec->type == JobType::Copy ? UndoEntry::Kind::Copy
        : spec->type == JobType::Move    ? UndoEntry::Kind::Move
                                         : UndoEntry::Kind::Trash;
    e.isUndo = byUndo;
    for (const JobOutcome &o : outcomes) {
        e.items << qMakePair(o.source, o.target);
        if (o.replaced) {
            ++e.replaced;
            e.replacedTargets << o.target;
        }
    }
    const QString what = e.items.size() == 1 ? Location::fileName(e.items.first().first)
                                             : QStringLiteral("%1 items").arg(e.items.size());
    switch (e.kind) {
    case UndoEntry::Kind::Copy:
        e.title = QStringLiteral("Copy %1 to %2").arg(what, Location::fileName(spec->destDir));
        break;
    case UndoEntry::Kind::Move:
        e.title = QStringLiteral("Move %1 to %2").arg(what, Location::fileName(spec->destDir));
        break;
    default:
        e.title = QStringLiteral("Move %1 to the trash").arg(what);
        break;
    }
    if (byUndo)
        e.title = QStringLiteral("Undo: ") + e.title;
    add(e);
}

bool UndoHistory::trashTargets(const UndoEntry &e, QStringList *touched, QString *error)
{
    Q_UNUSED(touched)
    UndoEntry inverse;
    inverse.kind = UndoEntry::Kind::Trash;
    inverse.isUndo = true;
    QStringList problems;
    int kept = 0;
    for (const auto &it : e.items) {
        const QString &path = it.second;
        if (e.replacedTargets.contains(path))
            continue; // overwrote a file that was there: taking it away would not bring that back
        if (!QFileInfo::exists(path) && !QFileInfo(path).isSymLink()) {
            problems << QStringLiteral("%1 is no longer there").arg(Location::fileName(path));
            continue;
        }
        QString trashed;
        QString why;
        if (!Trash::moveToTrash(path, &trashed, &why)) {
            problems << QStringLiteral("%1: %2").arg(Location::fileName(path), why);
            continue;
        }
        inverse.items << qMakePair(path, trashed);
        ++kept;
    }
    if (!inverse.items.isEmpty()) {
        inverse.title = QStringLiteral("Undo: %1").arg(e.title);
        add(inverse);
    }
    if (!problems.isEmpty() && error)
        *error = problems.join(QLatin1Char('\n'));
    return kept > 0 || e.items.isEmpty();
}

bool UndoHistory::undo(int id, QStringList *touched, QString *error)
{
    int index = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == id)
            index = i;
    }
    if (index < 0) {
        if (error)
            *error = QStringLiteral("That action is no longer in the undo list.");
        return false;
    }
    const UndoEntry e = m_entries.takeAt(index);
    Q_EMIT changed();
    QStringList problems;
    bool ok = true;

    switch (e.kind) {
    case UndoEntry::Kind::Copy:
    case UndoEntry::Kind::NewItem:
    case UndoEntry::Kind::Restore: {
        // Copies and new items go to the trash (recoverable); so does a file
        // brought back from the trash when that is undone.
        if (e.replaced > 0)
            problems << QStringLiteral("%1 of these copies replaced a file that was there, so they stay.").arg(e.replaced);
        QString why;
        ok = trashTargets(e, touched, &why);
        if (!why.isEmpty())
            problems << why;
        break;
    }
    case UndoEntry::Kind::Trash: {
        // Back from the trash to where each item was.
        UndoEntry inverse;
        inverse.kind = UndoEntry::Kind::Restore;
        inverse.isUndo = true;
        for (const auto &it : e.items) {
            const QString &original = it.first;
            const QString &trashed = it.second;
            if (QFileInfo::exists(original)) {
                problems << QStringLiteral("Something is already called %1").arg(Location::fileName(original));
                continue;
            }
            if (!QFileInfo::exists(trashed) && !QFileInfo(trashed).isSymLink()) {
                problems << QStringLiteral("%1 is no longer in the trash").arg(Location::fileName(original));
                continue;
            }
            QDir().mkpath(QFileInfo(original).absolutePath());
            if (std::rename(QFile::encodeName(trashed).constData(), QFile::encodeName(original).constData()) != 0) {
                problems << QStringLiteral("%1 could not be put back").arg(Location::fileName(original));
                continue;
            }
            // Its note in the trash's info folder goes too.
            const QString filesDir = QFileInfo(trashed).absolutePath();
            QFile::remove(QFileInfo(filesDir).absolutePath() + QStringLiteral("/info/") + QFileInfo(trashed).fileName()
                          + QStringLiteral(".trashinfo"));
            inverse.items << qMakePair(trashed, original);
            if (touched)
                *touched << original;
        }
        if (!inverse.items.isEmpty()) {
            inverse.title = QStringLiteral("Undo: %1").arg(e.title);
            add(inverse);
        }
        ok = !inverse.items.isEmpty();
        break;
    }
    case UndoEntry::Kind::Move: {
        // Each item back where it came from: a rename when it can be, a move
        // job (with progress) across devices or onto something in the way.
        UndoEntry inverse;
        inverse.kind = UndoEntry::Kind::Move;
        inverse.isUndo = true;
        QHash<QString, JobSpec> jobs; // by folder to move into
        for (const auto &it : e.items) {
            const QString &from = it.first; // where it was
            const QString &now = it.second; // where it is
            if (!QFileInfo::exists(now) && !QFileInfo(now).isSymLink()) {
                problems << QStringLiteral("%1 is no longer there").arg(Location::fileName(now));
                continue;
            }
            const QString folder = QFileInfo(from).absolutePath();
            QDir().mkpath(folder);
            if (!QFileInfo::exists(from) && FileOps::sameDevice(now, folder)
                && std::rename(QFile::encodeName(now).constData(), QFile::encodeName(from).constData()) == 0) {
                inverse.items << qMakePair(now, from);
                if (touched)
                    *touched << from;
                continue;
            }
            JobSpec &spec = jobs[folder];
            spec.type = JobType::Move;
            spec.destDir = folder;
            spec.sources << now;
            spec.targetNames.insert(now, QFileInfo(from).fileName());
            if (touched)
                *touched << from;
        }
        if (!inverse.items.isEmpty()) {
            inverse.title = QStringLiteral("Undo: %1").arg(e.title);
            add(inverse);
        }
        for (const JobSpec &spec : std::as_const(jobs))
            m_undoJobs << m_jobs.enqueue(spec);
        ok = !inverse.items.isEmpty() || !jobs.isEmpty();
        break;
    }
    case UndoEntry::Kind::Rename: {
        // items are (now, before): rename each back.
        QString why;
        if (!RenameEngine::undo(e.items, &why)) {
            problems << why;
            ok = false;
            m_entries.insert(index, e); // nothing changed: keep it
            Q_EMIT changed();
            break;
        }
        UndoEntry inverse;
        inverse.kind = UndoEntry::Kind::Rename;
        inverse.isUndo = true;
        for (const auto &it : e.items)
            Collections::instance()->renamed(it.first, it.second);
        for (const auto &it : e.items) {
            inverse.items << qMakePair(it.second, it.first);
            if (touched)
                *touched << it.second;
        }
        inverse.title = QStringLiteral("Undo: %1").arg(e.title);
        add(inverse);
        break;
    }
    }
    if (!problems.isEmpty() && error)
        *error = error->isEmpty() ? problems.join(QLatin1Char('\n')) : *error + QLatin1Char('\n') + problems.join(QLatin1Char('\n'));
    return ok;
}
