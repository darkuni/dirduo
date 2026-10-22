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
    refilter();
}

void FileSortProxy::refilter()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
}

void FileSortProxy::setFilter(const QString &text, bool invert, FilterScope scope)
{
    const QString t = text.trimmed();
    if (t == m_filterText && invert == m_filterInvert && scope == m_filterScope)
        return;
    m_filterText = t;
    m_filterInvert = invert;
    m_filterScope = scope;
    if (t.contains(QLatin1Char('*')) || t.contains(QLatin1Char('?'))) {
        QStringList parts;
        for (const QString &p : t.split(QLatin1Char(';'), Qt::SkipEmptyParts))
            parts << QRegularExpression::wildcardToRegularExpression(p.trimmed());
        m_filterRe = QRegularExpression(parts.join(QLatin1Char('|')), QRegularExpression::CaseInsensitiveOption);
    } else {
        m_filterRe = QRegularExpression(QRegularExpression::escape(t), QRegularExpression::CaseInsensitiveOption);
    }
    refilter();
}

void FileSortProxy::setGrouping(bool on)
{
    if (m_grouping == on)
        return;
    m_grouping = on;
    invalidate();
}

bool FileSortProxy::filterAcceptsRow(int sourceRow, const QModelIndex &) const
{
    const auto *model = static_cast<const FileListModel *>(sourceModel());
    const FileItem &item = model->item(sourceRow);
    if (!m_showHidden && item.isHidden)
        return false;
    if (m_filterText.isEmpty())
        return true;
    const bool inScope = m_filterScope == FilterScope::Both || (m_filterScope == FilterScope::Files && !item.isDir)
        || (m_filterScope == FilterScope::Folders && item.isDir);
    if (!inScope)
        return true; // the filter leaves the other kind alone
    const bool hit = m_filterRe.match(item.name).hasMatch();
    return hit != m_filterInvert;
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

    if (m_grouping) {
        const QString ga = left.sibling(left.row(), 0).data(FileListModel::GroupKeyRole).toString();
        const QString gb = right.sibling(right.row(), 0).data(FileListModel::GroupKeyRole).toString();
        if (ga != gb) {
            const bool ascending = sortOrder() == Qt::AscendingOrder;
            const bool less = FileOps::naturalCompare(ga, gb) < 0;
            return ascending ? less : !less;
        }
    }

    if (a.isDir != b.isDir) {
        // Keep folders on top whichever way the column is sorted.
        const bool ascending = sortOrder() == Qt::AscendingOrder;
        return ascending ? a.isDir : b.isDir;
    }

    switch (left.column()) {
    case FileListModel::SizeColumn: {
        // Folders sort by their measured size once it is known.
        const qint64 sa = a.isDir ? left.data(FileListModel::SortRole).toLongLong() : a.size;
        const qint64 sb = b.isDir ? right.data(FileListModel::SortRole).toLongLong() : b.size;
        if (sa != sb)
            return sa < sb;
        break;
    }
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
    case FileListModel::CreatedColumn:
        if (a.created != b.created)
            return a.created < b.created;
        break;
    case FileListModel::AccessedColumn:
        if (a.accessed != b.accessed)
            return a.accessed < b.accessed;
        break;
    case FileListModel::ExtensionColumn:
    case FileListModel::OwnerColumn:
    case FileListModel::GroupColumn:
    case FileListModel::LocationColumn: {
        const int c = FileOps::naturalCompare(left.data(FileListModel::SortRole).toString(),
                                              right.data(FileListModel::SortRole).toString());
        if (c != 0)
            return c < 0;
        break;
    }
    case FileListModel::PermissionsColumn:
        if (a.permissions != b.permissions)
            return static_cast<int>(a.permissions) < static_cast<int>(b.permissions);
        break;
    default:
        break;
    }
    return nameLess(left, right);
}
