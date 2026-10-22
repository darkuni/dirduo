#include "ui/FolderTab.h"

#include "core/DirLister.h"
#include "core/FileKind.h"
#include "core/FileOps.h"
#include "core/FolderSizes.h"
#include "core/Location.h"
#include "core/RemoteBackend.h"
#include "ui/FileListModel.h"
#include "ui/FileSortProxy.h"
#include "ui/GroupTreeView.h"
#include "ui/RenameDelegate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

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
    , m_stack(new QStackedLayout)
    , m_details(new GroupTreeView(this))
    , m_list(new QListView(this))
    , m_delegate(new RenameDelegate(this))
    , m_listDelegate(new RenameDelegate(this))
{
    m_proxy->setSourceModel(m_model);
    m_selection = new QItemSelectionModel(m_proxy, this);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->addWidget(m_details);
    m_stack->addWidget(m_list);
    outer->addLayout(m_stack, 1);

    // Filter bar (Ctrl+F): hides what does not match, live as you type.
    m_filterBar = new QWidget(this);
    auto *fl = new QHBoxLayout(m_filterBar);
    fl->setContentsMargins(4, 3, 4, 3);
    fl->setSpacing(6);
    m_filterEdit = new QLineEdit(m_filterBar);
    m_filterEdit->setPlaceholderText(QStringLiteral("Filter: text, or wildcards like *.mp3;*.flac"));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterInvert = new QCheckBox(QStringLiteral("Hide matches"), m_filterBar);
    m_filterScope = new QComboBox(m_filterBar);
    m_filterScope->addItem(QStringLiteral("Files"));
    m_filterScope->addItem(QStringLiteral("Folders"));
    m_filterScope->addItem(QStringLiteral("Both"));
    auto *closeFilter = new QToolButton(m_filterBar);
    closeFilter->setAutoRaise(true);
    closeFilter->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    closeFilter->setToolTip(QStringLiteral("Close the filter (Esc)"));
    fl->addWidget(m_filterEdit, 1);
    fl->addWidget(m_filterInvert);
    fl->addWidget(m_filterScope);
    fl->addWidget(closeFilter);
    m_filterBar->hide();
    outer->addWidget(m_filterBar);
    auto applyFilter = [this]() {
        m_proxy->setFilter(m_filterEdit->text(), m_filterInvert->isChecked(),
                           static_cast<FileSortProxy::FilterScope>(m_filterScope->currentIndex()));
        Q_EMIT statusChanged();
    };
    connect(m_filterEdit, &QLineEdit::textChanged, this, applyFilter);
    connect(m_filterInvert, &QCheckBox::toggled, this, applyFilter);
    connect(m_filterScope, &QComboBox::currentIndexChanged, this, applyFilter);
    connect(closeFilter, &QToolButton::clicked, this, [this]() { setFilterBarVisible(false); });
    m_filterEdit->installEventFilter(this);

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
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QWidget::customContextMenuRequested, this,
            [this, header](const QPoint &pos) { showHeaderMenu(header->mapToGlobal(pos)); });
    resetColumns();
    m_details->setSelectionModel(m_selection);
    m_delegate->setGroupView(m_details);

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
    v->viewport()->setMouseTracking(true); // folder sizes fill in on hover

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
    applyFormatFor(path);
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
        autoSwitchForContent();
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
    if (!m_proxy->filterText().isEmpty()) {
        const int hidden = m_model->rowCount() - m_proxy->rowCount();
        text += QStringLiteral(" - filter \"%1\"").arg(m_proxy->filterText());
        if (hidden > 0)
            text += QStringLiteral(", %1 hidden").arg(hidden);
    }
    if (m_lister->flatMode() != DirLister::FlatMode::Off) {
        text += QStringLiteral(" - flat view");
        if (m_lister->lastListingTruncated())
            text += QStringLiteral(" (stopped at %1 items)").arg(DirLister::kFlatLimit);
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
    if (watched == m_filterEdit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            setFilterBarVisible(false);
            return true;
        }
        if (ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            focusView();
            return true;
        }
        return false;
    }
    const bool isView = watched == m_details || watched == m_list;
    const bool isViewport = watched == m_details->viewport() || watched == m_list->viewport();
    if (!isView && !isViewport)
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::FocusIn:
    case QEvent::MouseButtonPress:
        Q_EMIT activated();
        break;
    case QEvent::Resize:
        if (watched == m_details->viewport())
            fitNameColumn();
        break;
    case QEvent::MouseMove: {
        // Hovering a folder measures it; the size fills in when ready.
        if (!isViewport || !Location::isLocal(m_path))
            break;
        auto *me = static_cast<QMouseEvent *>(event);
        const QModelIndex idx = view()->indexAt(me->position().toPoint());
        if (idx.isValid() && idx.data(FileListModel::IsDirRole).toBool())
            m_model->requestFolderSize(m_proxy->mapToSource(idx).row());
        break;
    }
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

// ---- Columns ---------------------------------------------------------------

bool FolderTab::isColumnVisible(int column) const
{
    return !m_details->header()->isSectionHidden(column);
}

void FolderTab::setColumnVisible(int column, bool visible)
{
    if (column <= FileListModel::NameColumn || column >= FileListModel::ColumnCount)
        return; // the name column always shows
    QHeaderView *h = m_details->header();
    if (h->isSectionHidden(column) == !visible)
        return;
    h->setSectionHidden(column, !visible);
    if (visible && h->sectionSize(column) < 40)
        h->resizeSection(column, column == FileListModel::LocationColumn ? 220 : 120);
    fitNameColumn();
    Q_EMIT viewStateChanged();
}

void FolderTab::resetColumns()
{
    QHeaderView *h = m_details->header();
    for (int c = 0; c < FileListModel::ColumnCount; ++c) {
        const bool standard = c == FileListModel::NameColumn || c == FileListModel::SizeColumn
            || c == FileListModel::TypeColumn || c == FileListModel::ModifiedColumn;
        h->setSectionHidden(c, !standard);
        if (c > FileListModel::ModifiedColumn) {
            h->setSectionResizeMode(c, QHeaderView::Interactive);
            h->resizeSection(c, c == FileListModel::LocationColumn ? 220 : 120);
        }
    }
    updateLocationColumn();
    Q_EMIT viewStateChanged();
}

void FolderTab::updateLocationColumn()
{
    // The Location column only means something in flat view.
    const bool flat = m_lister->flatMode() != DirLister::FlatMode::Off;
    m_details->header()->setSectionHidden(FileListModel::LocationColumn, !flat);
    fitNameColumn();
}

void FolderTab::showHeaderMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    menu.addSection(QStringLiteral("Columns"));
    for (int c = FileListModel::SizeColumn; c < FileListModel::ColumnCount; ++c) {
        QAction *a = menu.addAction(FileListModel::columnName(c));
        a->setCheckable(true);
        a->setChecked(isColumnVisible(c));
        connect(a, &QAction::toggled, this, [this, c](bool on) { setColumnVisible(c, on); });
    }
    QMenu *group = menu.addMenu(QStringLiteral("Group By"));
    using G = FileListModel::GroupBy;
    for (G g : {G::None, G::Type, G::Name, G::Size, G::Modified, G::Extension, G::Folder}) {
        QString label = FileListModel::groupByName(g);
        label[0] = label[0].toUpper();
        QAction *a = group->addAction(g == G::None ? QStringLiteral("No Grouping") : label);
        a->setCheckable(true);
        a->setChecked(groupBy() == g);
        connect(a, &QAction::triggered, this, [this, g]() { setGroupBy(g); });
    }
    menu.addSeparator();
    QAction *sizes = menu.addAction(QStringLiteral("Calculate All Folder Sizes"));
    sizes->setCheckable(true);
    sizes->setChecked(liveFolderSizes());
    connect(sizes, &QAction::toggled, this, &FolderTab::setLiveFolderSizes);
    QAction *checks = menu.addAction(QStringLiteral("Check Boxes"));
    checks->setCheckable(true);
    checks->setChecked(checkMode());
    connect(checks, &QAction::toggled, this, &FolderTab::setCheckMode);
    menu.addSeparator();
    connect(menu.addAction(QStringLiteral("Reset Columns")), &QAction::triggered, this, &FolderTab::resetColumns);
    menu.exec(globalPos);
}

// ---- Grouping --------------------------------------------------------------

void FolderTab::setGroupBy(FileListModel::GroupBy g)
{
    const bool on = g != FileListModel::GroupBy::None;
    m_model->setGroupBy(g);
    m_proxy->setGrouping(on);
    m_details->setGrouping(on);
    m_details->doItemsLayout();
    Q_EMIT viewStateChanged();
}

FileListModel::GroupBy FolderTab::groupBy() const
{
    return m_model->groupBy();
}

// ---- Filter ----------------------------------------------------------------

void FolderTab::setFilterBarVisible(bool visible)
{
    m_filterBar->setVisible(visible);
    if (visible) {
        m_filterEdit->setFocus(Qt::ShortcutFocusReason);
        m_filterEdit->selectAll();
    } else {
        // Closing the bar drops the filter, as in Opus.
        m_filterEdit->clear();
        focusView();
    }
    Q_EMIT viewStateChanged();
}

bool FolderTab::filterBarVisible() const
{
    return !m_filterBar->isHidden();
}

void FolderTab::setFilter(const QString &text, bool invert, FileSortProxy::FilterScope scope)
{
    {
        const QSignalBlocker b1(m_filterEdit), b2(m_filterInvert), b3(m_filterScope);
        m_filterEdit->setText(text);
        m_filterInvert->setChecked(invert);
        m_filterScope->setCurrentIndex(static_cast<int>(scope));
    }
    m_proxy->setFilter(text, invert, scope);
    if (!text.isEmpty())
        m_filterBar->setVisible(true);
    Q_EMIT statusChanged();
    Q_EMIT viewStateChanged();
}

QString FolderTab::filterText() const
{
    return m_proxy->filterText();
}

// ---- Check boxes and folder sizes ------------------------------------------

void FolderTab::setCheckMode(bool on)
{
    m_delegate->setCheckMode(on, m_selection);
    m_listDelegate->setCheckMode(on, m_selection);
    m_details->doItemsLayout();
    m_list->doItemsLayout();
    m_details->viewport()->update();
    m_list->viewport()->update();
    Q_EMIT viewStateChanged();
}

bool FolderTab::checkMode() const
{
    return m_delegate->checkMode();
}

void FolderTab::setLiveFolderSizes(bool on)
{
    m_model->setLiveFolderSizes(on);
    Q_EMIT viewStateChanged();
}

bool FolderTab::liveFolderSizes() const
{
    return m_model->liveFolderSizes();
}

// ---- Flat view -------------------------------------------------------------

void FolderTab::setFlatMode(DirLister::FlatMode mode)
{
    const DirLister::FlatMode old = m_lister->flatMode();
    if (old == mode)
        return;
    // Grouped flat view groups by folder; going back restores the old grouping.
    if (mode == DirLister::FlatMode::Grouped) {
        if (old != DirLister::FlatMode::Grouped)
            m_groupBeforeFlat = groupBy();
        setGroupBy(FileListModel::GroupBy::Folder);
    } else if (old == DirLister::FlatMode::Grouped) {
        setGroupBy(m_groupBeforeFlat);
    }
    m_model->clear(m_path);
    m_lister->setFlatMode(mode);
    updateLocationColumn();
    Q_EMIT statusChanged();
    Q_EMIT viewStateChanged();
}

DirLister::FlatMode FolderTab::flatMode() const
{
    return m_lister->flatMode();
}

QString FolderTab::flatModeName(DirLister::FlatMode mode)
{
    switch (mode) {
    case DirLister::FlatMode::Off:
        return QStringLiteral("off");
    case DirLister::FlatMode::Grouped:
        return QStringLiteral("grouped");
    case DirLister::FlatMode::Mixed:
        return QStringLiteral("mixed");
    case DirLister::FlatMode::MixedNoFolders:
        return QStringLiteral("nofolders");
    }
    return QStringLiteral("off");
}

bool FolderTab::flatModeFromName(const QString &name, DirLister::FlatMode &out)
{
    const QString n = name.toLower();
    if (n == QLatin1String("off") || n == QLatin1String("none"))
        out = DirLister::FlatMode::Off;
    else if (n == QLatin1String("grouped") || n == QLatin1String("on"))
        out = DirLister::FlatMode::Grouped;
    else if (n == QLatin1String("mixed"))
        out = DirLister::FlatMode::Mixed;
    else if (n == QLatin1String("nofolders") || n == QLatin1String("mixednofolders"))
        out = DirLister::FlatMode::MixedNoFolders;
    else
        return false;
    return true;
}

// ---- Folder formats --------------------------------------------------------

FolderFormat FolderTab::currentFormat() const
{
    FolderFormat f;
    f.path = m_path;
    f.header = headerState();
    f.groupBy = FileListModel::groupByName(groupBy());
    f.view = viewModeName(m_autoIcons ? m_modeBeforeAuto : m_mode);
    return f;
}

void FolderTab::formatSaved()
{
    if (m_formatActive)
        return;
    m_formatActive = true;
    m_baseFormat = FolderFormat(); // standard: details, no grouping, standard columns
    m_baseFormat.view = viewModeName(ViewMode::Details);
    m_baseFormat.groupBy = FileListModel::groupByName(FileListModel::GroupBy::None);
}

void FolderTab::applyFormat(const FolderFormat &format)
{
    if (!format.header.isEmpty())
        restoreHeaderState(format.header);
    else
        resetColumns();
    FileListModel::GroupBy g;
    if (FileListModel::groupByFromName(format.groupBy, g) && m_lister->flatMode() != DirLister::FlatMode::Grouped)
        setGroupBy(g);
    ViewMode mode;
    if (viewModeFromName(format.view, mode))
        setViewMode(mode);
    updateLocationColumn();
}

void FolderTab::applyFormatFor(const QString &path)
{
    // Undo the picture-folder switch from the previous folder.
    if (m_autoIcons) {
        m_autoIcons = false;
        setViewMode(m_modeBeforeAuto);
    }
    FolderFormat f;
    const bool own = FolderFormats::instance().find(path, f);
    if (own) {
        if (!m_formatActive) {
            m_baseFormat = currentFormat(); // remember the look to come back to
            m_formatActive = true;
        }
        applyFormat(f);
        return;
    }
    // No format of its own: the default format when there is one, else the
    // look from before the last formatted folder.
    FolderFormat base;
    if (FolderFormats::instance().defaultFormat(base)) {
        m_formatActive = false;
        applyFormat(base);
    } else if (m_formatActive) {
        m_formatActive = false;
        applyFormat(m_baseFormat);
    }
}

void FolderTab::autoSwitchForContent()
{
    if (m_formatActive || !FolderFormats::instance().autoSwitch() || m_mode != ViewMode::Details)
        return;
    if (m_lister->flatMode() != DirLister::FlatMode::Off)
        return;
    int files = 0;
    int pictures = 0;
    for (int r = 0; r < m_model->rowCount(); ++r) {
        const FileItem &it = m_model->item(r);
        if (it.isDir)
            continue;
        ++files;
        if (FileKind::categoryFor(it.name, false) == FileKind::Category::Image)
            ++pictures;
    }
    // Mostly pictures: icons. Any other folder keeps the view it had.
    if (files >= 4 && pictures * 10 >= files * 7) {
        m_modeBeforeAuto = m_mode;
        m_autoIcons = true;
        setViewMode(ViewMode::Icons);
    }
}

void FolderTab::fitNameColumn()
{
    // The name column takes the room left over, but never less than enough to
    // read a name: on a narrow display the list scrolls sideways instead.
    QHeaderView *h = m_details->header();
    const int minName = fontMetrics().averageCharWidth() * 22;
    int others = 0;
    for (int c = 0; c < FileListModel::ColumnCount; ++c) {
        if (c != FileListModel::NameColumn && !h->isSectionHidden(c))
            others += h->sectionSize(c);
    }
    const bool roomy = m_details->viewport()->width() - others >= minName;
    if (roomy && h->sectionResizeMode(FileListModel::NameColumn) != QHeaderView::Stretch) {
        h->setSectionResizeMode(FileListModel::NameColumn, QHeaderView::Stretch);
    } else if (!roomy && h->sectionResizeMode(FileListModel::NameColumn) == QHeaderView::Stretch) {
        h->setSectionResizeMode(FileListModel::NameColumn, QHeaderView::Interactive);
        h->resizeSection(FileListModel::NameColumn, minName);
    }
}
