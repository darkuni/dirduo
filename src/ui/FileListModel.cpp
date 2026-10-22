#include "ui/FileListModel.h"

#include "core/FileOps.h"

#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <QLocale>
#include <QMimeData>
#include <QPalette>
#include <QSet>
#include <QStyle>

FileListModel::FileListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void FileListModel::rebuildIndex()
{
    m_rowByPath.clear();
    m_rowByPath.reserve(m_items.size());
    for (int i = 0; i < m_items.size(); ++i)
        m_rowByPath.insert(m_items.at(i).path, i);
}

void FileListModel::setListing(const QString &dir, const FileItemList &items)
{
    beginResetModel();
    m_dir = dir;
    m_items = items;
    m_mimeNames = QVector<QString>(m_items.size());
    rebuildIndex();
    endResetModel();
}

void FileListModel::clear(const QString &dir)
{
    setListing(dir, {});
}

void FileListModel::updateListing(const FileItemList &items)
{
    QHash<QString, int> incoming;
    incoming.reserve(items.size());
    for (int i = 0; i < items.size(); ++i)
        incoming.insert(items.at(i).path, i);

    // A wholesale change (a folder that was emptied and refilled) is cheaper
    // as a reset.
    int kept = 0;
    for (const FileItem &it : std::as_const(m_items))
        kept += incoming.contains(it.path) ? 1 : 0;
    if (m_items.size() > 50 && kept < m_items.size() / 2) {
        setListing(m_dir, items);
        return;
    }

    // Removals, from the bottom so row numbers stay valid.
    for (int row = m_items.size() - 1; row >= 0; --row) {
        if (incoming.contains(m_items.at(row).path))
            continue;
        int first = row;
        while (first > 0 && !incoming.contains(m_items.at(first - 1).path))
            --first;
        beginRemoveRows({}, first, row);
        m_items.remove(first, row - first + 1);
        m_mimeNames.remove(first, row - first + 1);
        endRemoveRows();
        row = first;
    }
    rebuildIndex();

    // Changes in place.
    for (int row = 0; row < m_items.size(); ++row) {
        const FileItem &fresh = items.at(incoming.value(m_items.at(row).path));
        if (!m_items.at(row).sameContentAs(fresh)) {
            const bool kindChanged = m_items.at(row).isDir != fresh.isDir || m_items.at(row).name != fresh.name;
            m_items[row] = fresh;
            if (kindChanged)
                m_mimeNames[row].clear();
            Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
        }
    }

    // Additions at the end; the proxy sorts them into place.
    FileItemList added;
    for (const FileItem &it : items) {
        if (!m_rowByPath.contains(it.path))
            added.append(it);
    }
    if (!added.isEmpty()) {
        const int first = m_items.size();
        beginInsertRows({}, first, first + added.size() - 1);
        m_items += added;
        m_mimeNames.resize(m_items.size());
        rebuildIndex();
        endInsertRows();
    }
}

int FileListModel::fileCount() const
{
    int n = 0;
    for (const FileItem &it : m_items)
        n += it.isDir ? 0 : 1;
    return n;
}

int FileListModel::folderCount() const
{
    return m_items.size() - fileCount();
}

int FileListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

int FileListModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QString FileListModel::mimeNameFor(int row) const
{
    QString &cached = m_mimeNames[row];
    if (cached.isEmpty()) {
        const FileItem &it = m_items.at(row);
        if (it.isDir)
            cached = QStringLiteral("inode/directory");
        else
            cached = m_mimeDb.mimeTypeForFile(it.path, QMimeDatabase::MatchExtension).name();
    }
    return cached;
}

QString FileListModel::typeName(int row) const
{
    const QString mime = mimeNameFor(row);
    auto it = m_typeCache.constFind(mime);
    if (it != m_typeCache.cend())
        return *it;
    QString comment = m_items.at(row).isDir ? QStringLiteral("Folder") : m_mimeDb.mimeTypeForName(mime).comment();
    if (comment.isEmpty())
        comment = mime;
    if (!comment.isEmpty())
        comment[0] = comment[0].toUpper();
    m_typeCache.insert(mime, comment);
    return comment;
}

QIcon FileListModel::iconFor(int row) const
{
    const QString mime = mimeNameFor(row);
    auto it = m_iconCache.constFind(mime);
    if (it != m_iconCache.cend())
        return *it;
    QIcon icon;
    if (m_items.at(row).isDir) {
        icon = QIcon::fromTheme(QStringLiteral("folder"));
    } else {
        const QMimeType type = m_mimeDb.mimeTypeForName(mime);
        icon = QIcon::fromTheme(type.iconName());
        if (icon.isNull())
            icon = QIcon::fromTheme(type.genericIconName());
        if (icon.isNull())
            icon = QIcon::fromTheme(QStringLiteral("text-x-generic"));
    }
    if (icon.isNull())
        icon = QApplication::style()->standardIcon(m_items.at(row).isDir ? QStyle::SP_DirIcon : QStyle::SP_FileIcon);
    m_iconCache.insert(mime, icon);
    return icon;
}

QVariant FileListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const FileItem &it = m_items.at(index.row());
    const int col = index.column();

    switch (role) {
    case Qt::DisplayRole:
        switch (col) {
        case NameColumn:
            return it.name;
        case SizeColumn:
            return it.isDir ? QString() : FileOps::formatSize(it.size);
        case TypeColumn:
            return typeName(index.row());
        case ModifiedColumn:
            return QLocale().toString(it.modified, QLocale::ShortFormat);
        }
        break;
    case Qt::EditRole:
        if (col == NameColumn)
            return it.name;
        break;
    case Qt::DecorationRole:
        if (col == NameColumn)
            return iconFor(index.row());
        break;
    case Qt::ToolTipRole:
        if (col == NameColumn && it.isSymlink)
            return QStringLiteral("Link to %1").arg(it.symlinkTarget);
        break;
    case Qt::ForegroundRole:
        if (it.isHidden) {
            QColor c = QApplication::palette().color(QPalette::Text);
            c.setAlphaF(0.55);
            return c;
        }
        break;
    case Qt::FontRole:
        if (col == NameColumn && it.isSymlink) {
            QFont f = QApplication::font();
            f.setItalic(true);
            return f;
        }
        break;
    case Qt::TextAlignmentRole:
        if (col == SizeColumn)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        break;
    case PathRole:
        return it.path;
    case IsDirRole:
        return it.isDir;
    case SortRole:
        switch (col) {
        case SizeColumn:
            return it.size;
        case TypeColumn:
            return typeName(index.row());
        case ModifiedColumn:
            return it.modified;
        default:
            return it.name;
        }
    }
    return {};
}

bool FileListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || index.column() != NameColumn)
        return false;
    const QString newName = value.toString().trimmed();
    const FileItem &it = m_items.at(index.row());
    if (newName.isEmpty() || newName == it.name)
        return false;
    Q_EMIT renameRequested(it.path, newName);
    // The real rename runs as a command; the refresh shows the result.
    return false;
}

QVariant FileListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case NameColumn:
        return QStringLiteral("Name");
    case SizeColumn:
        return QStringLiteral("Size");
    case TypeColumn:
        return QStringLiteral("Type");
    case ModifiedColumn:
        return QStringLiteral("Modified");
    }
    return {};
}

Qt::ItemFlags FileListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    if (index.column() == NameColumn)
        f |= Qt::ItemIsEditable;
    if (m_items.at(index.row()).isDir)
        f |= Qt::ItemIsDropEnabled;
    return f;
}

QStringList FileListModel::mimeTypes() const
{
    return {QStringLiteral("text/uri-list")};
}

QMimeData *FileListModel::mimeData(const QModelIndexList &indexes) const
{
    QList<QUrl> urls;
    QSet<int> rows;
    for (const QModelIndex &i : indexes) {
        if (i.isValid() && !rows.contains(i.row())) {
            rows.insert(i.row());
            urls << QUrl::fromLocalFile(m_items.at(i.row()).path);
        }
    }
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    return mime;
}

Qt::DropActions FileListModel::supportedDropActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

Qt::DropActions FileListModel::supportedDragActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

bool FileListModel::canDropMimeData(const QMimeData *data, Qt::DropAction, int, int, const QModelIndex &parent) const
{
    if (!data || !data->hasUrls())
        return false;
    if (parent.isValid() && !m_items.at(parent.row()).isDir)
        return false;
    return true;
}

bool FileListModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int, int, const QModelIndex &parent)
{
    if (!canDropMimeData(data, action, -1, -1, parent))
        return false;
    const QString target = parent.isValid() ? m_items.at(parent.row()).path : m_dir;
    Q_EMIT dropRequested(data->urls(), target, action);
    return true;
}
