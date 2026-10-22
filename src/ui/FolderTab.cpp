#include "ui/FolderTab.h"

#include "core/DirLister.h"
#include "core/FileOps.h"
#include "core/Location.h"
#include "core/RemoteBackend.h"
#include "ui/FileListModel.h"
#include "ui/FileSortProxy.h"
#include "ui/RenameDelegate.h"

#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QKeyEvent>
#include <QListView>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QStackedLayout>
#include <QTimer>
#include <QTreeView>

#include <algorithm>

FolderTab::FolderTab(QWidget *parent)
    : QWidget(parent)
    , m_lister(new DirLister(this))
    , m_model(new FileListModel(this))
    , m_proxy(new FileSortProxy(this))
    , m_stack(new QStackedLayout(this))
    , m_details(new QTreeView(this))
    , m_list(new QListView(this))
    , m_delegate(new RenameDelegate(this))
    , m_listDelegate(new RenameDelegate(this))
{
    m_proxy->setSourceModel(m_model);
    m_selection = new QItemSelectionModel(m_proxy, this);

    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->addWidget(m_details);
    m_stack->addWidget(m_list);

    // Details
    m_details->setRootIsDecorated(false);
    m_details->setItemsExpandable(false);
    m_details->setUniformRowHeights(true);
    m_details->setAllColumnsShowFocus(true);
    m_details->setAlternatingRowColors(true);
    m_details->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_details->setSortingEnabled(true);
    m_details->setModel(m_proxy);
    m_details->sortByColumn(FileListModel::NameColumn, Qt::AscendingOrder);
    QHeaderView *header = m_details->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(FileListModel::NameColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(FileListModel::SizeColumn, QHeaderView::Interactive);
    header->setSectionResizeMode(FileListModel::TypeColumn, QHeaderView::Interactive);
    header->setSectionResizeMode(FileListModel::ModifiedColumn, QHeaderView::Interactive);
    header->resizeSection(FileListModel::SizeColumn, 90);
    header->resizeSection(FileListModel::TypeColumn, 130);
    header->resizeSection(FileListModel::ModifiedColumn, 140);
    header->setSortIndicatorShown(true);
    header->setSectionsMovable(true);
    m_details->setSelectionModel(m_selection);

    // List and icons share one QListView.
    m_list->setModel(m_proxy);
    m_list->setModelColumn(FileListModel::NameColumn);
    m_list->setSelectionModel(m_selection);
    m_list->setUniformItemSizes(true);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);

    setupView(m_details);
    setupView(m_list);

    connect(m_lister, &DirLister::listed, this, &FolderTab::onListed);
    connect(m_lister, &DirLister::failed, this, &FolderTab::onFailed);
    connect(m_model, &FileListModel::renameRequested, this, &FolderTab::renameRequested);
    connect(m_model, &FileListModel::dropRequested, this, &FolderTab::dropRequested);
    connect(m_delegate, &RenameDelegate::travelRequested, this, &FolderTab::travelRename);
    connect(m_listDelegate, &RenameDelegate::travelRequested, this, &FolderTab::travelRename);
    connect(m_selection, &QItemSelectionModel::selectionChanged, this, [this]() {
        Q_EMIT selectionChanged();
        Q_EMIT statusChanged();
    });
    connect(m_proxy, &QAbstractItemModel::rowsInserted, this, &FolderTab::statusChanged);
    connect(m_proxy, &QAbstractItemModel::rowsRemoved, this, &FolderTab::statusChanged);
    connect(m_proxy, &QAbstractItemModel::modelReset, this, &FolderTab::statusChanged);

    setViewMode(ViewMode::Details);
}

void FolderTab::setupView(QAbstractItemView *v)
{
    // One delegate per view: a delegate's commit/close signals reach every
    // view it is attached to.
    v->setItemDelegate(v == m_details ? m_delegate : m_listDelegate);
    v->setSelectionMode(QAbstractItemView::ExtendedSelection);
    v->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    v->setDragEnabled(true);
    v->setAcceptDrops(true);
    v->setDropIndicatorShown(true);
    v->setDragDropMode(QAbstractItemView::DragDrop);
    v->setDefaultDropAction(Qt::CopyAction);
    v->setContextMenuPolicy(Qt::CustomContextMenu);
    v->installEventFilter(this);
    v->viewport()->installEventFilter(this);

    // Double-click opens, whatever the desktop's single-click setting says.
    connect(v, &QAbstractItemView::doubleClicked, this, &FolderTab::activateIndex);
    connect(v, &QWidget::customContextMenuRequested, this, [this, v](const QPoint &pos) {
        const QModelIndex idx = v->indexAt(pos);
        if (!idx.isValid()) {
            m_selection->clearSelection();
        } else if (!m_selection->isSelected(idx)) {
            m_selection->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        Q_EMIT contextMenuRequested(v->viewport()->mapToGlobal(pos), idx.isValid());
    });
}

QAbstractItemView *FolderTab::view() const
{
    return m_mode == ViewMode::Details ? static_cast<QAbstractItemView *>(m_details) : m_list;
}

void FolderTab::focusView()
{
    view()->setFocus(Qt::OtherFocusReason);
}

QString FolderTab::title() const
{
    const QString name = Location::title(m_path);
    return name.isEmpty() ? m_path : name;
}

void FolderTab::navigate(const QString &rawPath, bool addToHistory)
{
    const QString path = Location::clean(rawPath);
    if (path == m_path) {
        refresh();
        return;
    }
    if (addToHistory && !m_path.isEmpty()) {
        m_back.append(m_path);
        if (m_back.size() > 100)
            m_back.removeFirst();
        m_forward.clear();
    }
    m_path = path;
    m_error.clear();
    m_pendingSelection.clear();
    m_model->clear(path);
    m_lister->open(path);
    Q_EMIT pathChanged(m_path);
    Q_EMIT statusChanged();
}

bool FolderTab::back()
{
    if (m_back.isEmpty())
        return false;
    m_forward.append(m_path);
    const QString target = m_back.takeLast();
    navigate(target, false);
    return true;
}

bool FolderTab::forward()
{
    if (m_forward.isEmpty())
        return false;
    m_back.append(m_path);
    const QString target = m_forward.takeLast();
    navigate(target, false);
    return true;
}

bool FolderTab::up()
{
    if (m_path.isEmpty() || Location::isRoot(m_path))
        return false;
    const QString from = m_path;
    const QString parent = Location::parent(m_path);
    navigate(parent);
    m_pendingSelection = {from}; // land on the folder we came out of
    return true;
}

void FolderTab::refresh()
{
    m_lister->refresh();
}

void FolderTab::onListed(const QString &path, const FileItemList &items, bool isRefresh)
{
    if (path != m_path)
        return;
    m_error.clear();
    if (isRefresh) {
        m_model->updateListing(items);
    } else {
        m_model->setListing(path, items);
        if (m_pendingSelection.isEmpty() && m_proxy->rowCount() > 0)
            m_selection->setCurrentIndex(m_proxy->index(0, 0), QItemSelectionModel::NoUpdate);
    }
    applyPendingSelection();
    Q_EMIT statusChanged();
}

void FolderTab::onFailed(const QString &path, const QString &error)
{
    if (path != m_path)
        return;
    // The folder vanished while open (deleted, card pulled): back out to the
    // nearest folder that still exists.
    if (Location::isLocal(path) && !QFileInfo::exists(path)) {
        QString parent = path;
        while (parent != QLatin1String("/") && !QFileInfo(parent).isDir())
            parent = QFileInfo(parent).absolutePath();
        if (parent != path) {
            navigate(parent, false);
            return;
        }
    }
    m_error = error;
    m_model->clear(path);
    Q_EMIT statusChanged();
}

QStringList FolderTab::selectedPaths() const
{
    QList<QModelIndex> rows;
    const QModelIndexList indexes = m_selection->selectedIndexes();
    for (const QModelIndex &i : indexes) {
        if (i.column() == FileListModel::NameColumn)
            rows << i;
    }
    std::sort(rows.begin(), rows.end(), [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });
    QStringList out;
    for (const QModelIndex &i : std::as_const(rows))
        out << i.data(FileListModel::PathRole).toString();
    return out;
}

QString FolderTab::focusedPath() const
{
    const QModelIndex cur = m_selection->currentIndex();
    return cur.isValid() ? cur.data(FileListModel::PathRole).toString() : QString();
}

QString FolderTab::statusText() const
{
    if (!m_error.isEmpty())
        return QStringLiteral("Cannot open this folder: %1").arg(m_error);
    if (m_lister->isBusy() && m_model->rowCount() == 0)
        return QStringLiteral("Reading folder...");

    int folders = 0;
    int files = 0;
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        if (m_proxy->index(r, 0).data(FileListModel::IsDirRole).toBool())
            ++folders;
        else
            ++files;
    }
    QString text = QStringLiteral("%1 %2, %3 %4")
                       .arg(folders)
                       .arg(folders == 1 ? QStringLiteral("folder") : QStringLiteral("folders"))
                       .arg(files)
                       .arg(files == 1 ? QStringLiteral("file") : QStringLiteral("files"));

    const QStringList sel = selectedPaths();
    if (!sel.isEmpty()) {
        qint64 bytes = 0;
        for (const QString &p : sel) {
            const int row = m_model->rowForPath(p);
            if (row >= 0 && !m_model->item(row).isDir)
                bytes += m_model->item(row).size;
        }
        text += QStringLiteral(" - %1 selected (%2)").arg(sel.size()).arg(FileOps::formatSize(bytes));
    }
    return text;
}

void FolderTab::selectPaths(const QStringList &paths)
{
    m_pendingSelection = paths;
    if (m_model->rowCount() > 0)
        applyPendingSelection();
}

void FolderTab::applyPendingSelection()
{
    if (m_pendingSelection.isEmpty())
        return;
    if (isRenaming())
        return; // never pull focus out of an open rename editor

    QItemSelection selection;
    QModelIndex first;
    for (const QString &p : std::as_const(m_pendingSelection)) {
        const int row = m_model->rowForPath(Location::clean(p));
        if (row < 0)
            continue;
        const QModelIndex idx = m_proxy->mapFromSource(m_model->index(row, 0));
        if (!idx.isValid())
            continue;
        selection.select(idx, idx);
        if (!first.isValid() || idx.row() < first.row())
            first = idx;
    }
    if (!first.isValid())
        return; // not listed yet; keep waiting for the refresh
    m_pendingSelection.clear();
    m_selection->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_selection->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
    view()->scrollTo(first);
}

void FolderTab::selectAll()
{
    view()->selectAll();
}

void FolderTab::selectNone()
{
    m_selection->clearSelection();
}

void FolderTab::invertSelection()
{
    if (m_proxy->rowCount() == 0)
        return;
    const QItemSelection all(m_proxy->index(0, 0), m_proxy->index(m_proxy->rowCount() - 1, FileListModel::ColumnCount - 1));
    m_selection->select(all, QItemSelectionModel::Toggle);
}

int FolderTab::selectPattern(const QString &wildcard)
{
    const QRegularExpression re(QRegularExpression::wildcardToRegularExpression(wildcard),
                                QRegularExpression::CaseInsensitiveOption);
    QItemSelection selection;
    int count = 0;
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        const QModelIndex idx = m_proxy->index(r, 0);
        if (re.match(idx.data(Qt::DisplayRole).toString()).hasMatch()) {
            selection.select(idx, idx);
            ++count;
        }
    }
    m_selection->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    return count;
}

void FolderTab::setViewMode(ViewMode mode)
{
    m_mode = mode;
    switch (mode) {
    case ViewMode::Details:
        m_stack->setCurrentWidget(m_details);
        break;
    case ViewMode::List:
        m_list->setViewMode(QListView::ListMode);
        m_list->setFlow(QListView::TopToBottom);
        m_list->setWrapping(true);
        m_list->setIconSize(QSize(16, 16));
        m_list->setGridSize(QSize());
        m_list->setSpacing(1);
        m_list->setWordWrap(false);
        m_stack->setCurrentWidget(m_list);
        break;
    case ViewMode::Icons:
        m_list->setViewMode(QListView::IconMode);
        m_list->setFlow(QListView::LeftToRight);
        m_list->setWrapping(true);
        m_list->setIconSize(QSize(48, 48));
        m_list->setGridSize(QSize(104, 88));
        m_list->setSpacing(4);
        m_list->setWordWrap(true);
        m_list->setMovement(QListView::Static);
        m_stack->setCurrentWidget(m_list);
        break;
    }
    const QModelIndex cur = m_selection->currentIndex();
    if (cur.isValid())
        view()->scrollTo(cur);
}

void FolderTab::setShowHidden(bool show)
{
    m_proxy->setShowHidden(show);
    Q_EMIT statusChanged();
}

bool FolderTab::showHidden() const
{
    return m_proxy->showHidden();
}

bool FolderTab::isRenaming() const
{
    return m_delegate->isEditing() || m_listDelegate->isEditing();
}

bool FolderTab::beginRename()
{
    QModelIndex idx = m_selection->currentIndex();
    if (!idx.isValid() || !m_selection->isSelected(idx)) {
        const QModelIndexList sel = m_selection->selectedIndexes();
        if (!sel.isEmpty())
            idx = sel.first();
    }
    if (!idx.isValid())
        return false;
    idx = idx.sibling(idx.row(), FileListModel::NameColumn);
    m_selection->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view()->scrollTo(idx);
    view()->setFocus();
    view()->edit(idx);
    return true;
}

void FolderTab::travelRename(int delta)
{
    const QModelIndex cur = m_selection->currentIndex();
    if (!cur.isValid())
        return;
    const int row = cur.row() + delta;
    if (row < 0 || row >= m_proxy->rowCount())
        return;
    // The next item, taken as a persistent index so a refresh that reorders
    // rows after the rename still finds it.
    const QPersistentModelIndex next(m_proxy->index(row, FileListModel::NameColumn));
    QTimer::singleShot(0, this, [this, next]() {
        if (!next.isValid())
            return;
        m_selection->setCurrentIndex(next, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        view()->scrollTo(next);
        view()->edit(next);
    });
}

void FolderTab::activateIndex(const QModelIndex &proxyIndex)
{
    if (!proxyIndex.isValid())
        return;
    const QString path = proxyIndex.data(FileListModel::PathRole).toString();
    if (proxyIndex.data(FileListModel::IsDirRole).toBool()) {
        navigate(path);
        return;
    }
    // Archives open as folders, Opus style; "Open in Application" from the
    // context menu still hands them to an archiver.
    if (Location::isLocal(path)) {
        if (RemoteBackend *backend = RemoteBackend::instance()) {
            const QString inside = backend->archiveLocation(path);
            if (!inside.isEmpty()) {
                navigate(inside);
                return;
            }
        }
    }
    Q_EMIT openFileRequested(path);
}

bool FolderTab::isFolder(const QString &path) const
{
    const int row = m_model->rowForPath(Location::clean(path));
    if (row >= 0)
        return m_model->item(row).isDir;
    return Location::isLocal(path) && QFileInfo(path).isDir();
}

bool FolderTab::eventFilter(QObject *watched, QEvent *event)
{
    const bool isView = watched == m_details || watched == m_list;
    const bool isViewport = watched == m_details->viewport() || watched == m_list->viewport();
    if (!isView && !isViewport)
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::FocusIn:
    case QEvent::MouseButtonPress:
        Q_EMIT activated();
        break;
    case QEvent::MouseButtonRelease: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (isViewport && me->button() == Qt::MiddleButton) {
            const QModelIndex idx = view()->indexAt(me->position().toPoint());
            if (idx.isValid() && idx.data(FileListModel::IsDirRole).toBool()) {
                Q_EMIT openInNewTabRequested(idx.data(FileListModel::PathRole).toString());
                return true;
            }
        }
        if (isViewport && me->button() == Qt::BackButton) {
            back();
            return true;
        }
        if (isViewport && me->button() == Qt::ForwardButton) {
            forward();
            return true;
        }
        break;
    }
    case QEvent::KeyPress: {
        if (!isView || isRenaming())
            break;
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
            Q_EMIT tabKeyPressed(ke->key() == Qt::Key_Backtab);
            return true;
        }
        if (ke->key() == Qt::Key_Backspace && ke->modifiers() == Qt::NoModifier) {
            up();
            return true;
        }
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && ke->modifiers() == Qt::NoModifier) {
            const QStringList sel = selectedPaths();
            const QModelIndex cur = m_selection->currentIndex();
            if (sel.size() <= 1 && cur.isValid()) {
                activateIndex(cur);
            } else {
                // Several selected: open each file; folders need a double-click.
                for (const QString &p : sel) {
                    if (!isFolder(p))
                        Q_EMIT openFileRequested(p);
                }
            }
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

QByteArray FolderTab::headerState() const
{
    return m_details->header()->saveState();
}

void FolderTab::restoreHeaderState(const QByteArray &state)
{
    if (!state.isEmpty())
        m_details->header()->restoreState(state);
}

QString FolderTab::viewModeName(ViewMode mode)
{
    switch (mode) {
    case ViewMode::Details:
        return QStringLiteral("details");
    case ViewMode::List:
        return QStringLiteral("list");
    case ViewMode::Icons:
        return QStringLiteral("icons");
    }
    return QStringLiteral("details");
}

bool FolderTab::viewModeFromName(const QString &name, ViewMode &out)
{
    const QString n = name.toLower();
    if (n == QLatin1String("details"))
        out = ViewMode::Details;
    else if (n == QLatin1String("list"))
        out = ViewMode::List;
    else if (n == QLatin1String("icons") || n == QLatin1String("largeicons"))
        out = ViewMode::Icons;
    else
        return false;
    return true;
}
