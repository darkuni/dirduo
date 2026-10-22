#pragma once

#include "core/DriveList.h"

#include <QAbstractItemModel>
#include <QFileSystemWatcher>
#include <QIcon>
#include <QTimer>
#include <QTreeView>

#include <memory>
#include <vector>

class AliasManager;

// What the folder tree shows: Home, the drives (root, SD cards, USB sticks,
// other disks) and the user's aliases at the top, each opening into its real
// folders. Folders are read when first expanded, and folders that are open
// are watched so new and removed subfolders show up.
class FolderTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    struct Root
    {
        QString label;
        QString path;
        QString icon;
        QString group; // non-empty: shown under a heading node of that name
    };

    explicit FolderTreeModel(QObject *parent = nullptr);
    ~FolderTreeModel() override;

    void setRoots(const QList<Root> &roots);
    QList<Root> roots() const { return m_roots; }
    void setShowHidden(bool show);

    QString pathOf(const QModelIndex &index) const;
    // The node for path under the closest root, reading folders on the way.
    // Invalid when no root holds it.
    QModelIndex indexForPath(const QString &path);

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool hasChildren(const QModelIndex &parent = {}) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    struct Node;
    Node *nodeOf(const QModelIndex &index) const;
    QModelIndex indexOf(Node *node) const;
    QStringList listFolders(const QString &path) const;
    void fetch(Node *node);
    void refresh(Node *node);
    void onDirectoryChanged(const QString &path);
    void forEachFetched(Node *node, const std::function<void(Node *)> &fn);

    std::unique_ptr<Node> m_root;
    QList<Root> m_roots;
    bool m_showHidden = false;
    QFileSystemWatcher m_watcher;
    QIcon m_folderIcon;
};

// The folder tree beside the file list. It follows the folder shown in its
// display (or, as the shared tree, the source display) and clicking a folder
// reads it in.
class FolderTree : public QTreeView
{
    Q_OBJECT
public:
    explicit FolderTree(AliasManager *aliases = nullptr, QWidget *parent = nullptr);

    // Selects and reveals path. Remote locations leave the tree as it is.
    void setPath(const QString &path);
    QString currentFolder() const;
    void setShowHidden(bool show);
    void setZoom(int percent); // follows the lister's zoom
    FolderTreeModel *treeModel() const { return m_model; }

    // Rebuilds the top level from the drives now (mounts are also polled).
    void rescan();
    void setDrivesForTesting(const QList<DriveEntry> &drives);

Q_SIGNALS:
    void folderActivated(const QString &path);

private:
    void rebuild(const QList<DriveEntry> &drives);
    void reveal();

    AliasManager *m_aliases;
    FolderTreeModel *m_model;
    QList<FolderTreeModel::Root> m_lastRoots;
    QTimer m_poll;
    QString m_path;
    bool m_syncing = false;
    int m_zoom = 100;
};
