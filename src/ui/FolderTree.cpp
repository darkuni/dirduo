#include "ui/FolderTree.h"

#include "core/Location.h"
#include "ui/Style.h"

#include <QDir>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QTimer>

FolderTree::FolderTree(QWidget *parent)
    : QTreeView(parent)
    , m_model(new QFileSystemModel(this))
{
    m_model->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);
    m_model->setReadOnly(true);
    m_model->setRootPath(QStringLiteral("/"));
    setModel(m_model);
    setRootIndex(m_model->index(QStringLiteral("/")).parent());
    for (int c = 1; c < m_model->columnCount(); ++c)
        hideColumn(c);
    header()->hide();
    setUniformRowHeights(true);
    setAnimated(false);
    setIndentation(14);
    setExpandsOnDoubleClick(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setMinimumWidth(120);
    setFrameShape(QFrame::NoFrame);

    // Click, Enter or arrow keys: the display follows.
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &cur) {
        if (m_syncing || !cur.isValid())
            return;
        const QString path = m_model->filePath(cur);
        if (path.isEmpty() || path == m_path)
            return;
        m_path = path;
        Q_EMIT folderActivated(path);
    });
    // Folders load in the background; keep the current one in view as they do.
    connect(m_model, &QFileSystemModel::directoryLoaded, this, [this](const QString &dir) {
        if (!m_path.isEmpty() && Location::isSameOrInside(m_path, dir))
            reveal();
    });
    setPath(QDir::homePath());
}

void FolderTree::setShowHidden(bool show)
{
    QDir::Filters f = QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives;
    if (show)
        f |= QDir::Hidden;
    m_model->setFilter(f);
}

void FolderTree::setPath(const QString &path)
{
    if (!Location::isLocal(path))
        return;
    m_path = Location::clean(path);
    reveal();
}

QString FolderTree::currentFolder() const
{
    return m_path;
}

void FolderTree::reveal()
{
    const QModelIndex idx = m_model->index(m_path);
    if (!idx.isValid())
        return;
    m_syncing = true;
    for (QModelIndex p = idx.parent(); p.isValid(); p = p.parent())
        expand(p);
    selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
    scrollTo(idx, QAbstractItemView::EnsureVisible);
    m_syncing = false;
}
