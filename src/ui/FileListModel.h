#pragma once

#include "core/FileItem.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QIcon>
#include <QMimeDatabase>
#include <QUrl>

// The rows of one folder. Updates are applied as a diff so selection, scroll
// position and an open rename editor survive a refresh.
class FileListModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { NameColumn, SizeColumn, TypeColumn, ModifiedColumn, ColumnCount };
    enum Role {
        PathRole = Qt::UserRole + 1,
        IsDirRole,
        SortRole,
    };

    explicit FileListModel(QObject *parent = nullptr);

    void setListing(const QString &dir, const FileItemList &items); // new folder
    void updateListing(const FileItemList &items);                  // same folder, changed
    void clear(const QString &dir);

    QString dir() const { return m_dir; }
    const FileItem &item(int row) const { return m_items.at(row); }
    int rowForPath(const QString &path) const { return m_rowByPath.value(path, -1); }
    int fileCount() const;
    int folderCount() const;

    QString typeName(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDropActions() const override;
    Qt::DropActions supportedDragActions() const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;

Q_SIGNALS:
    // Inline rename finished; the tab turns this into a Rename command.
    void renameRequested(const QString &path, const QString &newName);
    // Files dropped on the folder (targetDir) or on a folder row.
    void dropRequested(const QList<QUrl> &urls, const QString &targetDir, Qt::DropAction action);

private:
    void rebuildIndex();
    QIcon iconFor(int row) const;
    QString mimeNameFor(int row) const;

    QString m_dir;
    FileItemList m_items;
    QHash<QString, int> m_rowByPath;
    mutable QVector<QString> m_mimeNames; // lazily filled, parallel to m_items
    mutable QHash<QString, QIcon> m_iconCache;
    mutable QHash<QString, QString> m_typeCache;
    QMimeDatabase m_mimeDb;
};
