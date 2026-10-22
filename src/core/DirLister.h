#pragma once

#include "core/FileItem.h"

#include <QFileSystemWatcher>

#include <atomic>
#include <memory>
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

    // Flat view: the folder and everything under it in one list.
    enum class FlatMode { Off, Grouped, Mixed, MixedNoFolders };
    void setFlatMode(FlatMode mode); // re-lists
    FlatMode flatMode() const { return m_flat; }

    void open(const QString &path);
    void refresh();
    QString path() const { return m_path; }
    bool isBusy() const;

    // Synchronous listing, shared with tests.
    static FileItemList listNow(const QString &path, QString *error);
    // Everything under path; relPath set on each item. Stops at maxItems.
    static FileItemList listRecursive(const QString &path, bool includeFolders, int maxItems, bool *truncated,
                                      QString *error, const std::atomic<quint64> *generation = nullptr,
                                      quint64 expected = 0);
    static constexpr int kFlatLimit = 200000;

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
    FlatMode m_flat = FlatMode::Off;
    bool m_truncated = false;
    std::shared_ptr<std::atomic<quint64>> m_liveGeneration;

public:
    bool lastListingTruncated() const { return m_truncated; }
};
