#pragma once

#include <QTreeView>

class QFileSystemModel;

// The folder tree beside the file list. Folders only, rooted at /, with the
// home folder opened at start. It follows the folder shown in its display (or,
// as the shared tree, the source display) and clicking a folder reads it in.
class FolderTree : public QTreeView
{
    Q_OBJECT
public:
    explicit FolderTree(QWidget *parent = nullptr);

    // Selects and reveals path. Remote locations leave the tree as it is.
    void setPath(const QString &path);
    QString currentFolder() const;
    void setShowHidden(bool show);

Q_SIGNALS:
    void folderActivated(const QString &path);

private:
    void reveal();

    QFileSystemModel *m_model;
    QString m_path;
    bool m_syncing = false;
};
