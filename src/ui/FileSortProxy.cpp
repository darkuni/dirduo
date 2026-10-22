#include "ui/FileSortProxy.h"

#include "ui/FileListModel.h"

#include "core/FileOps.h"

#include <QDateTime>

FileSortProxy::FileSortProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setSortRole(FileListModel::SortRole);
    setDynamicSortFilter(true);
}

void FileSortProxy::setShowHidden(bool show)
{
    if (m_showHidden == show)
        return;
    m_showHidden = show;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
}

bool FileSortProxy::filterAcceptsRow(int sourceRow, const QModelIndex &) const
{
    if (m_showHidden)
        return true;
    const auto *model = static_cast<const FileListModel *>(sourceModel());
    return !model->item(sourceRow).isHidden;
}

bool FileSortProxy::nameLess(const QModelIndex &left, const QModelIndex &right) const
{
    const auto *model = static_cast<const FileListModel *>(sourceModel());
    return FileOps::naturalCompare(model->item(left.row()).name, model->item(right.row()).name) < 0;
}

bool FileSortProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const auto *model = static_cast<const FileListModel *>(sourceModel());
    const FileItem &a = model->item(left.row());
    const FileItem &b = model->item(right.row());

    if (a.isDir != b.isDir) {
        // Keep folders on top whichever way the column is sorted.
        const bool ascending = sortOrder() == Qt::AscendingOrder;
        return ascending ? a.isDir : b.isDir;
    }

    switch (left.column()) {
    case FileListModel::SizeColumn:
        if (a.size != b.size)
            return a.size < b.size;
        break;
    case FileListModel::TypeColumn: {
        const int c = QString::localeAwareCompare(model->typeName(left.row()), model->typeName(right.row()));
        if (c != 0)
            return c < 0;
        break;
    }
    case FileListModel::ModifiedColumn:
        if (a.modified != b.modified)
            return a.modified < b.modified;
        break;
    default:
        break;
    }
    return nameLess(left, right);
}
