#pragma once

#include "core/Job.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class JobManager;

// Every file operation, kept on disk (the last few thousand), so a file that
// went missing can be traced: what happened to it, when, and where it went.
// ~/.local/share/dirduo/file-log.tsv, one line per item.
namespace FileLog {

struct Entry
{
    QDateTime when;
    QString action; // "Copy", "Move", "Trash", "Delete", "Rename", "New folder", "Undo copy"...
    QString item;   // the file or folder
    QString to;     // where it went (copy, move, rename, trash)
    QString note;   // an error, or "replaced a file"
};

QString file();
void append(const QList<Entry> &entries);
void append(const Entry &entry);
QList<Entry> read(); // oldest first
void clear();
// Text for saving or the clipboard.
QString toText(const QList<Entry> &entries);
constexpr int kKeep = 5000;

} // namespace FileLog

// What can be undone this session, newest first, as in Opus: copy, move, new
// folders and files, sending to the trash, and renames. Undoing an action is
// recorded as an action too, so undoing the undo puts it back (redo).
// Copies come out by going to the trash, never deleted outright.
struct UndoEntry
{
    enum class Kind { Copy, Move, Trash, Restore, NewItem, Rename, Tags, Labels };
    int id = 0;
    Kind kind = Kind::Copy;
    QDateTime when;
    QString title;                         // "Copy 3 items to Backups"
    QList<QPair<QString, QString>> items;  // (from, to) for each item
    int replaced = 0;                      // copies that overwrote a file (kept on undo)
    QStringList replacedTargets;           // which ones
    bool isUndo = false;                   // made by undoing something
    QList<QVariantMap> before;             // Tags: each item's fields as they were (MediaEdit::toMap);
                                           // Labels: FileTagData::toMap
    QList<QPair<QString, QString>> trashedSources; // Extract and trash: (archive, in the trash), put back on undo
};

class UndoHistory : public QObject
{
    Q_OBJECT
public:
    explicit UndoHistory(JobManager &jobs, QObject *parent = nullptr);

    QList<UndoEntry> entries() const { return m_entries; } // newest first
    bool canUndo() const { return !m_entries.isEmpty(); }
    const UndoEntry *latest() const { return m_entries.isEmpty() ? nullptr : &m_entries.first(); }
    // The newest rename made by hand (for Rename UNDO).
    const UndoEntry *latestRename() const;

    // Adds an entry (and file log lines); returns its id.
    int add(UndoEntry entry);
    // Undoes one entry. touched receives the paths that now exist again, to
    // select them. Moves between devices go through the job manager and show
    // progress; everything else happens at once.
    bool undo(int id, QStringList *touched, QString *error);

    static constexpr int kKeep = 100;
    static QString kindName(UndoEntry::Kind kind);

Q_SIGNALS:
    void changed();

private:
    void onJobFinished(int id);
    // Trashes each item's "to"; the inverse is a Trash entry.
    bool trashTargets(const UndoEntry &e, QStringList *touched, QString *error);

    JobManager &m_jobs;
    QList<UndoEntry> m_entries;
    int m_nextId = 1;
    bool m_undoing = false; // jobs started by an undo are recorded as undos
    QList<int> m_undoJobs;
};
