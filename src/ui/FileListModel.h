#pragma once

#include "core/FileItem.h"
#include "core/Metadata.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QIcon>
#include <QMimeDatabase>
#include <QSet>
#include <QUrl>

// The rows of one folder. Updates are applied as a diff so selection, scroll
// position and an open rename editor survive a refresh.
class FileListModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        NameColumn,
        SizeColumn,
        TypeColumn,
        ModifiedColumn,
        CreatedColumn,
        AccessedColumn,
        ExtensionColumn,
        PermissionsColumn,
        OwnerColumn,
        GroupColumn,
        LocationColumn, // flat view: the folder an item is in
        // From the files themselves, read in the background (Metadata).
        TitleColumn,
        ArtistColumn,
        AlbumColumn,
        TrackColumn,
        YearColumn,
        GenreColumn,
        DurationColumn,
        BitrateColumn,
        DimensionsColumn,
        TakenColumn,
        CameraColumn,
        ColumnCount
    };
    // Saved column layouts from before the metadata columns had this many.
    static constexpr int ColumnsBeforeMetadata = TitleColumn;
    static bool isMetadataColumn(int column) { return column >= TitleColumn && column < ColumnCount; }
    enum Role {
        PathRole = Qt::UserRole + 1,
        IsDirRole,
        SortRole,
        GroupKeyRole,   // sorts the groups
        GroupLabelRole, // heading shown above a group
        FolderBytesRole, // qint64, -1 while unknown
        RelPathRole,
        FolderBarRole,   // 0..1 share of the biggest folder (live sizes), -1 for none
    };
    enum class GroupBy { None, Type, Name, Size, Modified, Extension, Folder, Artist, Album, Genre, Year, Camera, Taken };
    static QList<GroupBy> allGroupings();
    static QString groupByLabel(GroupBy g); // for menus: "No Grouping", "Date Taken"
    static bool isMetadataGrouping(GroupBy g) { return g >= GroupBy::Artist; }

    void setGroupBy(GroupBy g);
    GroupBy groupBy() const { return m_groupBy; }
    static QString groupByName(GroupBy g);
    static bool groupByFromName(const QString &name, GroupBy &out);

    // Live folder sizes: every folder's size worked out as soon as it is listed.
    void setLiveFolderSizes(bool on);
    bool liveFolderSizes() const { return m_liveSizes; }
    qint64 maxFolderBytes() const { return m_maxFolderBytes; }
    void requestFolderSize(int row);

    static QString columnName(int column);

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
    // Icon and thumbnail views: pictures (and anything with a cached
    // thumbnail) show as themselves.
    void setThumbnails(bool on);
    bool thumbnails() const { return m_thumbs; }

    // Tags and photo details of a row, if read; starts reading otherwise.
    bool mediaInfo(int row, MediaInfo *out) const;

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
    void requestAllFolderSizes();
    void onFolderSizeReady(const QString &path);
    void onMetadataReady(const QString &path);
    void onThumbnailReady(const QString &path);
    QVariant thumbnailFor(int row) const;
    QVariant metadataValue(int row, int column, bool forSorting) const;
    QPair<QString, QString> groupFor(int row) const;
    QString folderSizeText(int row) const;
    QIcon iconFor(int row) const;
    QString mimeNameFor(int row) const;

    QString m_dir;
    FileItemList m_items;
    QHash<QString, int> m_rowByPath;
    mutable QVector<QString> m_mimeNames; // lazily filled, parallel to m_items
    mutable QHash<QString, QIcon> m_iconCache;
    mutable QHash<QString, QString> m_typeCache;
    QMimeDatabase m_mimeDb;
    GroupBy m_groupBy = GroupBy::None;
    bool m_liveSizes = false;
    qint64 m_maxFolderBytes = 0;
    QSet<int> m_metaRows; // rows whose details came in, announced together
    bool m_metaPending = false;
    bool m_thumbs = false;
    mutable QHash<QString, QIcon> m_thumbIcons; // framed, ready to paint
    QSet<int> m_thumbRows;
    bool m_thumbPending = false;
};
