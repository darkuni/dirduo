#pragma once

#include "core/FileItem.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

class RemoteLister;

// Lists one folder off the GUI thread and re-lists it when it changes on disk.
// Results from an older request are dropped, so fast navigation never shows a
// stale folder. Remote locations go to the RemoteBackend's lister.
class DirLister : public QObject
{
    Q_OBJECT
public:
    explicit DirLister(QObject *parent = nullptr);

    void open(const QString &path);
    void refresh();
    QString path() const { return m_path; }
    bool isBusy() const;

    // Synchronous listing, shared with tests.
    static FileItemList listNow(const QString &path, QString *error);

Q_SIGNALS:
    // isRefresh: the same folder was listed again (keep selection and scroll).
    void listed(const QString &path, const FileItemList &items, bool isRefresh);
    void failed(const QString &path, const QString &error);

private:
    void start(bool isRefresh);
    void watch();
    void openRemote(bool isRefresh);

    QString m_path;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    quint64 m_generation = 0;
    bool m_busy = false;
    bool m_refreshPending = false;
    RemoteLister *m_remote = nullptr;
};
