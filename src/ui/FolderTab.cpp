#include "ui/FolderTab.h"
#include "core/FileIndex.h"
#include <QInputDialog>
#include "core/SavedFilters.h"
#include "core/Prefs.h"
#include "core/StatusFormat.h"
#include "core/Scripting.h"
#include "core/CommandCatalog.h"

#include "core/Collections.h"
#include "core/Favorites.h"
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
#include "ui/Style.h"

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
#include <QPointer>
#include <QProxyStyle>
#include <QKeyEvent>
#include <QApplication>
#include <QListView>
#include <QToolTip>
#include <QLabel>
#include <QNativeGestureEvent>
#include <QGestureEvent>
#include <QWheelEvent>
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
    // Saved filters (Preferences > Filters): pick one, or keep this one.
    auto *saved = new QToolButton(m_filterBar);
    saved->setObjectName(QStringLiteral("filter-saved"));
    saved->setAutoRaise(true);
    saved->setIcon(QIcon::fromTheme(QStringLiteral("bookmarks")));
    saved->setText(QStringLiteral("Saved"));
    saved->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    saved->setToolTip(QStringLiteral("Saved filters"));
    saved->setPopupMode(QToolButton::InstantPopup);
    auto *savedMenu = new QMenu(saved);
    saved->setMenu(savedMenu);
    connect(savedMenu, &QMenu::aboutToShow, this, [this, savedMenu]() {
        savedMenu->clear();
        for (const SavedFilters::Filter &f : SavedFilters::instance()->filters()) {
            QAction *a = savedMenu->addAction(f.name);
            a->setToolTip((f.hide ? QStringLiteral("Hides ") : QString()) + f.pattern);
            a->setCheckable(true);
            a->setChecked(!m_filterEdit->text().isEmpty() && m_filterEdit->text() == f.pattern);
            connect(a, &QAction::triggered, this, [this, f]() {
                setFilter(f.pattern, f.hide,
                          f.scope == QLatin1String("folders") ? FileSortProxy::FilterScope::Folders
                          : f.scope == QLatin1String("both")  ? FileSortProxy::FilterScope::Both
                                                              : FileSortProxy::FilterScope::Files);
            });
        }
        savedMenu->setToolTipsVisible(true);
        savedMenu->addSeparator();
        QAction *keep = savedMenu->addAction(QIcon::fromTheme(QStringLiteral("document-save")), QStringLiteral("Save This Filter..."));
        keep->setEnabled(!m_filterEdit->text().isEmpty());
        connect(keep, &QAction::triggered, this, [this]() {
            bool ok = false;
            const QString name = QInputDialog::getText(this, QStringLiteral("Save Filter"),
                                                       QStringLiteral("Name for the filter \"%1\":").arg(m_filterEdit->text()),
                                                       QLineEdit::Normal, QString(), &ok).trimmed();
            if (!ok || name.isEmpty())
                return;
            SavedFilters::Filter f;
            f.name = name;
            f.pattern = m_filterEdit->text();
            f.hide = m_filterInvert->isChecked();
            f.scope = m_filterScope->currentIndex() == 1 ? QStringLiteral("folders")
                : m_filterScope->currentIndex() == 2   ? QStringLiteral("both")
                                                       : QStringLiteral("files");
            SavedFilters::instance()->save(f);
        });
    });
    fl->addWidget(m_filterEdit, 1);
    fl->addWidget(saved);
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

    // Find-as-you-type field, under the list.
    m_fayt = new QWidget(this);
    auto *tl = new QHBoxLayout(m_fayt);
    tl->setContentsMargins(4, 3, 4, 3);
    tl->setSpacing(6);
    m_faytLabel = new QLabel(m_fayt);
    m_faytLabel->setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("Command (99)")));
    m_faytEdit = new QLineEdit(m_fayt);
    m_faytEdit->setToolTip(QStringLiteral("Find: Up/Down for the next match, Enter opens\n"
                                          "Wildcards (*.d64) select what matches\n"
                                          ">command runs it (Tab completes)\n"
                                          "/alias or ~/path goes there\n"
                                          "Ctrl+F makes it a filter; Esc closes"));
    auto *toFilter = new QToolButton(m_fayt);
    toFilter->setAutoRaise(true);
    toFilter->setIcon(QIcon::fromTheme(QStringLiteral("view-filter")));
    toFilter->setToolTip(QStringLiteral("Filter by this instead (Ctrl+F)"));
    auto *closeFayt = new QToolButton(m_fayt);
    closeFayt->setAutoRaise(true);
    closeFayt->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    closeFayt->setToolTip(QStringLiteral("Close (Esc)"));
    tl->addWidget(m_faytLabel);
    tl->addWidget(m_faytEdit, 1);
    tl->addWidget(toFilter);
    tl->addWidget(closeFayt);
    m_fayt->hide();
    outer->addWidget(m_fayt);
    m_faytEdit->installEventFilter(this);
    connect(m_faytEdit, &QLineEdit::textChanged, this, &FolderTab::updateTypeAhead);
    connect(closeFayt, &QToolButton::clicked, this, &FolderTab::closeTypeAhead);
    connect(toFilter, &QToolButton::clicked, this, [this]() {
        QKeyEvent ctrlF(QEvent::KeyPress, Qt::Key_F, Qt::ControlModifier);
        typeAheadKey(&ctrlF);
    });

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
    // Every column, the name too, is sized by dragging its right edge. The
    // name fills the room the others leave until it has been dragged;
    // double-clicking its edge makes it fill again.
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setMinimumSectionSize(24);
    header->setStyle(headerGripStyle());
    header->installEventFilter(this);
    connect(header, &QHeaderView::sectionResized, this, [this](int logical, int, int) {
        if (m_fittingName)
            return;
        if (logical == FileListModel::NameColumn && m_headerDragging) {
            m_nameFills = false;
            Q_EMIT viewStateChanged();
        } else if (logical != FileListModel::NameColumn) {
            fitNameColumn();
        }
    });
    connect(header, &QHeaderView::sectionHandleDoubleClicked, this, [this](int logical) {
        if (logical != FileListModel::NameColumn)
            return;
        QTimer::singleShot(0, this, [this]() {
            m_nameFills = true;
            fitNameColumn();
            Q_EMIT viewStateChanged();
        });
    });
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
    m_delegate->setTreeMode(true); // the details view's triangles
    connect(m_details, &GroupTreeView::collapsedChanged, this, &FolderTab::viewStateChanged);

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
    // The item in focus matters too (the viewer shows it).
    connect(m_selection, &QItemSelectionModel::currentChanged, this, &FolderTab::selectionChanged);
    connect(m_proxy, &QAbstractItemModel::rowsInserted, this, &FolderTab::statusChanged);
    connect(m_proxy, &QAbstractItemModel::rowsRemoved, this, &FolderTab::statusChanged);
    connect(m_proxy, &QAbstractItemModel::modelReset, this, &FolderTab::statusChanged);

    setViewMode(ViewMode::Details);
    applySize();
    connect(Style::instance(), &Style::changed, this, &FolderTab::applySize);
}

int FolderTab::zoomed(int px) const
{
    return qMax(1, int(Style::instance()->scaled(px) * m_zoom / 100.0 + 0.5));
}

void FolderTab::setZoom(int percent)
{
    percent = qBound(50, percent, 300);
    if (percent == m_zoom)
        return;
    // Columns grow and shrink with the text, so zoomed names still fit; the
    // list scrolls sideways when they no longer all do.
    const double factor = double(percent) / m_zoom;
    QHeaderView *h = m_details->header();
    for (int c = 0; c < h->count(); ++c) {
        if (h->sectionResizeMode(c) == QHeaderView::Interactive)
            h->resizeSection(c, qMax(24, int(h->sectionSize(c) * factor + 0.5)));
    }
    m_zoom = percent;
    applySize();
}

void FolderTab::applySize()
{
    // The app's text size, times this lister's zoom.
    QFont f = QApplication::font();
    if (m_zoom != 100) {
        if (f.pointSizeF() > 0)
            f.setPointSizeF(f.pointSizeF() * m_zoom / 100.0);
        else
            f.setPixelSize(qMax(6, int(f.pixelSize() * m_zoom / 100.0 + 0.5)));
    }
    for (QWidget *w : {static_cast<QWidget *>(m_details), static_cast<QWidget *>(m_list),
                       static_cast<QWidget *>(m_details->header())})
        w->setFont(f); // set again whenever the app's text size changes
    const int px = zoomed(16);
    m_details->setIconSize(QSize(px, px));
    setViewMode(m_mode); // list and icon sizes
    m_details->doItemsLayout();
    fitNameColumn();
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
    v->viewport()->grabGesture(Qt::PinchGesture); // two fingers zoom on the Deck's screen

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
    if (addToHistory)
        Favorites::instance()->visit(path); // frequent folders (history moves are not new visits)
    collapseAllFolders();
    m_model->clear(path);
    applyFormatFor(path);
    updateLocationColumn();
    updateExpandable();
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
    for (DirLister *l : std::as_const(m_children))
        l->refresh();
}

// ---- Expandable folders -----------------------------------------------------

void FolderTab::updateExpandable()
{
    const bool on = m_mode == ViewMode::Details && m_lister->flatMode() == DirLister::FlatMode::Off
        && !Collections::isCollectionLocation(m_path);
    if (!on)
        collapseAllFolders();
    m_model->setExpandable(on);
}

bool FolderTab::expandFolder(const QString &path)
{
    const int row = m_model->rowForPath(path);
    if (!m_model->canExpand(row) || m_children.contains(path))
        return false;
    auto *l = new DirLister(this);
    m_children.insert(path, l);
    m_model->setExpanded(path, true);
    connect(l, &DirLister::listed, this, [this, path](const QString &, const FileItemList &items, bool) {
        if (!m_children.contains(path))
            return;
        m_model->setChildren(path, items);
        Q_EMIT statusChanged();
    });
    connect(l, &DirLister::failed, this, [this, path](const QString &, const QString &error) {
        collapseFolder(path);
        m_error.clear();
        Q_EMIT statusChanged();
        Q_EMIT expandFailed(path, error);
    });
    l->open(path);
    return true;
}

void FolderTab::collapseFolder(const QString &path)
{
    if (!m_children.contains(path) && !m_model->isExpanded(path))
        return;
    // It and every folder open inside it.
    for (auto it = m_children.begin(); it != m_children.end();) {
        if (Location::isSameOrInside(it.key(), path)) {
            it.value()->deleteLater();
            it = m_children.erase(it);
        } else {
            ++it;
        }
    }
    m_model->removeDescendants(path);
    m_model->setExpanded(path, false);
    Q_EMIT statusChanged();
}

void FolderTab::toggleFolder(const QString &path)
{
    if (m_children.contains(path))
        collapseFolder(path);
    else
        expandFolder(path);
}

void FolderTab::collapseAllFolders()
{
    if (m_children.isEmpty())
        return;
    const QStringList tops = m_children.keys();
    for (const QString &p : tops) {
        if (m_children.contains(p))
            collapseFolder(p);
    }
}

bool FolderTab::expanderHit(const QPoint &pos, QModelIndex *index) const
{
    if (m_mode != ViewMode::Details)
        return false;
    QModelIndex idx = m_details->indexAt(pos);
    if (!idx.isValid())
        return false;
    idx = idx.siblingAtColumn(FileListModel::NameColumn);
    QStyleOptionViewItem opt;
    opt.initFrom(m_details);
    opt.rect = m_details->visualRect(idx);
    opt.decorationSize = m_details->iconSize();
    opt.widget = m_details;
    const QRect r = m_delegate->expanderRect(opt, idx);
    if (!r.isValid() || !r.adjusted(-2, -2, 2, 2).contains(pos))
        return false;
    if (index)
        *index = idx;
    return true;
}

void FolderTab::onListed(const QString &path, const FileItemList &items, bool isRefresh)
{
    if (path != m_path)
        return;
    FileIndex::instance()->folderListed(path, items); // keeps Index Search current
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

QStringList FolderTab::visiblePaths() const
{
    QStringList out;
    const QAbstractItemModel *m = m_selection->model();
    std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &parent) {
        for (int r = 0; r < m->rowCount(parent); ++r) {
            const QModelIndex i = m->index(r, FileListModel::NameColumn, parent);
            const QString p = i.data(FileListModel::PathRole).toString();
            if (!p.isEmpty())
                out << p;
            if (m->hasChildren(i))
                walk(i);
        }
    };
    walk(QModelIndex());
    return out;
}

QString FolderTab::focusedPath() const
{
    const QModelIndex cur = m_selection->currentIndex();
    return cur.isValid() ? cur.data(FileListModel::PathRole).toString() : QString();
}

QString FolderTab::statusMessage() const
{
    if (!m_error.isEmpty())
        return QStringLiteral("Cannot open this folder: %1").arg(m_error);
    if (m_lister->isBusy() && m_model->rowCount() == 0)
        return QStringLiteral("Reading folder...");
    return {};
}

QHash<QString, QString> FolderTab::statusValues(bool withDisk) const
{
    QHash<QString, QString> v;
    int folders = 0, files = 0;
    qint64 bytes = 0;
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        const QModelIndex i = m_proxy->index(r, 0);
        if (i.data(FileListModel::IsDirRole).toBool()) {
            ++folders;
        } else {
            ++files;
            const int row = m_proxy->mapToSource(i).row();
            if (row >= 0)
                bytes += m_model->item(row).size;
        }
    }
    int selFolders = 0, selFiles = 0;
    qint64 selBytes = 0;
    for (const QString &p : selectedPaths()) {
        const int row = m_model->rowForPath(p);
        if (row < 0)
            continue;
        if (m_model->item(row).isDir) {
            ++selFolders;
        } else {
            ++selFiles;
            selBytes += m_model->item(row).size;
        }
    }
    auto words = [](int n, const char *one, const char *many) {
        return QStringLiteral("%1 %2").arg(n).arg(QLatin1String(n == 1 ? one : many));
    };
    v.insert(QStringLiteral("td"), QString::number(folders));
    v.insert(QStringLiteral("tf"), QString::number(files));
    v.insert(QStringLiteral("ti"), QString::number(folders + files));
    v.insert(QStringLiteral("tb"), FileOps::formatSize(bytes));
    v.insert(QStringLiteral("folders"), words(folders, "folder", "folders"));
    v.insert(QStringLiteral("files"), words(files, "file", "files"));
    v.insert(QStringLiteral("items"), words(folders + files, "item", "items"));
    v.insert(QStringLiteral("sd"), QString::number(selFolders));
    v.insert(QStringLiteral("sf"), QString::number(selFiles));
    v.insert(QStringLiteral("si"), QString::number(selFolders + selFiles));
    v.insert(QStringLiteral("sb"), selFolders + selFiles > 0 ? FileOps::formatSize(selBytes) : QString());
    v.insert(QStringLiteral("selected"), selFolders + selFiles > 0 ? QStringLiteral("%1 selected").arg(selFolders + selFiles) : QString());
    const int hidden = m_model->rowCount() - m_proxy->rowCount();
    v.insert(QStringLiteral("hidden"), hidden > 0 ? QString::number(hidden) : QString());
    v.insert(QStringLiteral("filter"), m_proxy->filterText());
    QString flat;
    if (m_lister->flatMode() != DirLister::FlatMode::Off) {
        flat = QStringLiteral("flat view");
        if (m_lister->lastListingTruncated())
            flat += QStringLiteral(" (stopped at %1 items)").arg(DirLister::kFlatLimit);
    }
    v.insert(QStringLiteral("flat"), flat);
    const QString focus = focusedPath();
    const int frow = focus.isEmpty() ? -1 : m_model->rowForPath(focus);
    if (frow >= 0) {
        const FileItem &it = m_model->item(frow);
        v.insert(QStringLiteral("name"), it.name);
        v.insert(QStringLiteral("size"), it.isDir ? QString() : FileOps::formatSize(it.size));
        v.insert(QStringLiteral("modified"), QLocale().toString(it.modified, QLocale::ShortFormat));
        v.insert(QStringLiteral("type"), m_model->data(m_model->index(frow, FileListModel::TypeColumn), Qt::DisplayRole).toString());
    } else {
        for (const char *k : {"name", "size", "modified", "type"})
            v.insert(QLatin1String(k), QString());
    }
    v.insert(QStringLiteral("path"), Location::display(path()));
    v.insert(QStringLiteral("folder"), Location::title(path()));
    // The drive's space: folders on this computer and its cards only (asking a server is slow).
    for (const char *k : {"df", "dt", "du", "dp", "low"})
        v.insert(QLatin1String(k), QString());
    if (withDisk && Location::isLocal(path())) {
        const qint64 free = FileOps::bytesFree(path());
        const qint64 total = FileOps::bytesTotal(path());
        if (free >= 0 && total > 0) {
            v.insert(QStringLiteral("df"), FileOps::formatSize(free));
            v.insert(QStringLiteral("dt"), FileOps::formatSize(total));
            v.insert(QStringLiteral("du"), FileOps::formatSize(total - free));
            v.insert(QStringLiteral("dp"), QString::number(qRound(100.0 * double(total - free) / double(total))));
            // Nearly full (under 5%, or under 1 GB): shown in the warning color.
            v.insert(QStringLiteral("low"), (free < total / 20 || free < qint64(1) << 30) ? QStringLiteral("1") : QStringLiteral("0"));
        }
    }
    return v;
}

QString FolderTab::statusDefinition() const
{
    return m_statusDef.trimmed().isEmpty() ? Prefs::instance()->values().effectiveStatusDefinition() : m_statusDef;
}

QString FolderTab::statusText() const
{
    const QString msg = statusMessage();
    if (!msg.isEmpty())
        return msg;
    const QString def = statusDefinition();
    return StatusFormat::plainText(StatusFormat::render(def, statusValues(def.contains(QLatin1String("{d")) || def.contains(QLatin1String("{bar}")))), false);
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

int FolderTab::selectWhere(const std::function<bool(const FileItem &)> &test, SelectMode mode)
{
    QItemSelection selection;
    int count = 0;
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        const QModelIndex idx = m_proxy->index(r, 0);
        if (test(m_model->item(m_proxy->mapToSource(idx).row()))) {
            selection.select(idx, idx);
            ++count;
        }
    }
    const QItemSelectionModel::SelectionFlags how = mode == SelectMode::Replace ? QItemSelectionModel::ClearAndSelect
        : mode == SelectMode::Add                                               ? QItemSelectionModel::Select
                                                                                : QItemSelectionModel::Deselect;
    if (mode == SelectMode::Replace && selection.isEmpty())
        m_selection->clearSelection();
    else
        m_selection->select(selection, how | QItemSelectionModel::Rows);
    if (count > 0 && mode != SelectMode::Remove) {
        const QModelIndex first = selection.first().topLeft();
        m_selection->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
        view()->scrollTo(first);
    }
    return count;
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
        m_list->setIconSize(QSize(zoomed(16), zoomed(16)));
        m_list->setGridSize(QSize());
        m_list->setSpacing(1);
        m_list->setWordWrap(false);
        m_stack->setCurrentWidget(m_list);
        break;
    case ViewMode::Icons:
        m_list->setViewMode(QListView::IconMode);
        m_list->setFlow(QListView::LeftToRight);
        m_list->setWrapping(true);
        m_list->setIconSize(QSize(zoomed(48), zoomed(48)));
        m_list->setGridSize(QSize(zoomed(104), zoomed(88)));
        m_list->setSpacing(4);
        m_list->setWordWrap(true);
        m_list->setMovement(QListView::Static);
        m_stack->setCurrentWidget(m_list);
        break;
    case ViewMode::Thumbnails: {
        const int px = zoomed(128);
        m_list->setViewMode(QListView::IconMode);
        m_list->setFlow(QListView::LeftToRight);
        m_list->setWrapping(true);
        m_list->setIconSize(QSize(px, px));
        m_list->setGridSize(QSize(px + zoomed(24), px + m_list->fontMetrics().lineSpacing() * 2 + zoomed(12)));
        m_list->setSpacing(4);
        m_list->setWordWrap(true);
        m_list->setMovement(QListView::Static);
        m_stack->setCurrentWidget(m_list);
        break;
    }
    }
    // Pictures show as themselves in both icon views.
    m_model->setThumbnails(mode == ViewMode::Icons || mode == ViewMode::Thumbnails);
    updateExpandable();
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
    if (watched == m_faytEdit) {
        if (event->type() == QEvent::KeyPress && typeAheadKey(static_cast<QKeyEvent *>(event)))
            return true;
        if (event->type() == QEvent::ShortcutOverride) {
            // Keep window shortcuts (Ctrl+F, arrows, Tab) for the field.
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down
                || (ke->key() == Qt::Key_F && ke->modifiers() == Qt::ControlModifier) || ke->key() == Qt::Key_Escape) {
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::FocusOut) {
            // Clicking elsewhere closes it (after the click has landed).
            QTimer::singleShot(0, this, [this]() {
                if (m_fayt->isVisible() && !m_faytEdit->hasFocus())
                    closeTypeAhead();
            });
        }
        return QWidget::eventFilter(watched, event);
    }
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
    if (watched == m_details->header()) {
        if (event->type() == QEvent::MouseButtonPress)
            m_headerDragging = true;
        else if (event->type() == QEvent::MouseButtonRelease)
            m_headerDragging = false;
        return QWidget::eventFilter(watched, event);
    }
    const bool isView = watched == m_details || watched == m_list;
    const bool isViewport = watched == m_details->viewport() || watched == m_list->viewport();
    if (!isView && !isViewport)
        return QWidget::eventFilter(watched, event);

    if (watched == m_details->viewport()
        && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick
            || event->type() == QEvent::MouseButtonRelease)) {
        // A folder's triangle opens or closes it, and does nothing else.
        auto *me = static_cast<QMouseEvent *>(event);
        QModelIndex idx;
        if (me->button() == Qt::LeftButton && expanderHit(me->position().toPoint(), &idx)) {
            // Each click toggles (a double-click brings two presses: open,
            // then closed again); the double-click event itself is eaten so
            // the folder is not opened.
            if (event->type() == QEvent::MouseButtonPress) {
                Q_EMIT activated();
                toggleFolder(idx.data(FileListModel::PathRole).toString());
            }
            return true;
        }
    }

    switch (event->type()) {
    case QEvent::FocusIn:
    case QEvent::MouseButtonPress:
        Q_EMIT activated();
        break;
    case QEvent::Wheel: {
        // Shift+wheel zooms, as in Opus. Some systems turn Shift+wheel into a
        // sideways scroll, so either direction counts.
        auto *we = static_cast<QWheelEvent *>(event);
        if (!isViewport || !(we->modifiers() & Qt::ShiftModifier))
            break;
        const QPoint d = we->angleDelta();
        m_wheelZoom += d.y() != 0 ? d.y() : d.x();
        int steps = 0;
        while (m_wheelZoom >= 120) {
            ++steps;
            m_wheelZoom -= 120;
        }
        while (m_wheelZoom <= -120) {
            --steps;
            m_wheelZoom += 120;
        }
        if (steps)
            Q_EMIT zoomRequested(steps);
        return true;
    }
    case QEvent::NativeGesture: {
        // Pinch on a touchpad.
        auto *ge = static_cast<QNativeGestureEvent *>(event);
        if (ge->gestureType() != Qt::ZoomNativeGesture)
            break;
        m_pinchZoom *= 1.0 + ge->value();
        if (m_pinchZoom > 1.15 || m_pinchZoom < 0.87) {
            Q_EMIT zoomRequested(m_pinchZoom > 1 ? 1 : -1);
            m_pinchZoom = 1.0;
        }
        return true;
    }
    case QEvent::Gesture: {
        // Pinch on the touchscreen.
        auto *ge = static_cast<QGestureEvent *>(event);
        if (auto *pinch = static_cast<QPinchGesture *>(ge->gesture(Qt::PinchGesture))) {
            if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged) {
                m_pinchZoom *= pinch->scaleFactor();
                if (m_pinchZoom > 1.2 || m_pinchZoom < 0.83) {
                    Q_EMIT zoomRequested(m_pinchZoom > 1 ? 1 : -1);
                    m_pinchZoom = 1.0;
                }
            }
            if (pinch->state() == Qt::GestureFinished || pinch->state() == Qt::GestureCanceled)
                m_pinchZoom = 1.0;
            ge->accept(pinch);
            return true;
        }
        break;
    }
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
        {
            // A printable key opens find-as-you-type with it.
            const QString text = ke->text();
            const Qt::KeyboardModifiers mods = ke->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
            if (mods == Qt::NoModifier && text.size() == 1 && text.at(0).isPrint() && !text.at(0).isSpace()) {
                openTypeAhead(text);
                return true;
            }
        }
        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
            Q_EMIT tabKeyPressed(ke->key() == Qt::Key_Backtab);
            return true;
        }
        if (ke->key() == Qt::Key_Backspace && ke->modifiers() == Qt::NoModifier) {
            up();
            return true;
        }
        if (m_mode == ViewMode::Details && m_model->expandable() && ke->modifiers() == Qt::NoModifier
            && (ke->key() == Qt::Key_Right || ke->key() == Qt::Key_Left)) {
            // Right opens a folder in place, Left closes it or goes to the
            // folder an item is in. A folded group's heading keeps its keys.
            const QModelIndex cur = m_selection->currentIndex();
            if (cur.isValid() && !m_details->isCollapsed(cur)) {
                const int row = m_proxy->mapToSource(cur).row();
                const QString p = cur.data(FileListModel::PathRole).toString();
                if (ke->key() == Qt::Key_Right && m_model->canExpand(row)) {
                    if (!m_children.contains(p))
                        expandFolder(p);
                    return true;
                }
                if (ke->key() == Qt::Key_Left) {
                    if (m_children.contains(p)) {
                        collapseFolder(p);
                        return true;
                    }
                    const int parent = m_model->parentRow(row);
                    if (parent >= 0) {
                        const QModelIndex pi = m_proxy->mapFromSource(m_model->index(parent, 0));
                        m_selection->setCurrentIndex(pi, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                        m_details->scrollTo(pi);
                        return true;
                    }
                }
            }
        }
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && ke->modifiers() == Qt::NoModifier) {
            const QStringList sel = selectedPaths();
            const QModelIndex cur = m_selection->currentIndex();
            if (m_mode == ViewMode::Details && m_details->isCollapsed(cur)) {
                m_details->toggleGroup(cur); // Enter on a folded heading opens it
                return true;
            }
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

// Saved header states start with how many columns there were, so a layout
// saved before a column existed keeps that column hidden. States saved before
// this tag was added had FileListModel::ColumnsBeforeMetadata columns.
static const QByteArray kHeaderTag = QByteArrayLiteral("LNH1");

QByteArray FolderTab::headerState() const
{
    QByteArray out = kHeaderTag;
    out += QByteArray::number(FileListModel::ColumnCount);
    if (!m_nameFills)
        out += 'f'; // the name column was sized by hand
    out += ':';
    out += m_details->header()->saveState();
    return out;
}

void FolderTab::restoreHeaderState(const QByteArray &saved)
{
    if (saved.isEmpty())
        return;
    QByteArray state = saved;
    int columns = FileListModel::ColumnsBeforeMetadata;
    bool fills = true;
    if (state.startsWith(kHeaderTag)) {
        const int colon = state.indexOf(':', kHeaderTag.size());
        if (colon < 0)
            return;
        QByteArray count = state.mid(kHeaderTag.size(), colon - kHeaderTag.size());
        fills = !count.endsWith('f');
        if (!fills)
            count.chop(1);
        columns = count.toInt();
        state = state.mid(colon + 1);
    }
    QHeaderView *h = m_details->header();
    m_fittingName = true;
    const bool restored = h->restoreState(state);
    // Older states kept the name column stretched, which cannot be dragged.
    h->setSectionResizeMode(QHeaderView::Interactive);
    m_fittingName = false;
    if (!restored)
        return;
    m_nameFills = fills;
    for (int c = qMax(1, columns); c < FileListModel::ColumnCount; ++c) {
        h->setSectionHidden(c, true);
        h->resizeSection(c, 120);
    }
    updateLocationColumn();
    fitNameColumn();
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
    case ViewMode::Thumbnails:
        return QStringLiteral("thumbnails");
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
    else if (n == QLatin1String("thumbnails") || n == QLatin1String("thumbs"))
        out = ViewMode::Thumbnails;
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
    m_nameFills = true;
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
    // Flat view and collections show where each item lives.
    const bool flat = m_lister->flatMode() != DirLister::FlatMode::Off || Collections::isCollectionLocation(m_path);
    m_details->header()->setSectionHidden(FileListModel::LocationColumn, !flat);
    fitNameColumn();
}

void FolderTab::showHeaderMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    menu.addSection(QStringLiteral("Columns"));
    auto addColumn = [this](QMenu *m, int c) {
        QAction *a = m->addAction(FileListModel::columnName(c));
        a->setCheckable(true);
        a->setChecked(isColumnVisible(c));
        connect(a, &QAction::toggled, this, [this, c](bool on) { setColumnVisible(c, on); });
    };
    for (int c = FileListModel::SizeColumn; c < FileListModel::ColumnsBeforeMetadata; ++c)
        addColumn(&menu, c);
    using M = FileListModel;
    QMenu *music = menu.addMenu(QIcon::fromTheme(QStringLiteral("audio-x-generic")), QStringLiteral("Music"));
    for (int c : {M::TitleColumn, M::ArtistColumn, M::AlbumColumn, M::TrackColumn, M::YearColumn, M::GenreColumn,
                  M::DurationColumn, M::BitrateColumn})
        addColumn(music, c);
    QMenu *pictures = menu.addMenu(QIcon::fromTheme(QStringLiteral("image-x-generic")), QStringLiteral("Pictures"));
    for (int c : {M::DimensionsColumn, M::TakenColumn, M::CameraColumn})
        addColumn(pictures, c);
    QMenu *tagMenu = menu.addMenu(QIcon::fromTheme(QStringLiteral("tag")), QStringLiteral("Labels and Tags"));
    for (int c : {M::RatingColumn, M::LabelColumn, M::TagsColumn, M::DescriptionColumn})
        addColumn(tagMenu, c);
    if (ScriptHost *scripts = ScriptHost::instance(); scripts && !scripts->columns().isEmpty()) {
        QMenu *sm = menu.addMenu(QIcon::fromTheme(QStringLiteral("text-x-script")), QStringLiteral("Scripts"));
        for (const ScriptColumnDef &d : scripts->columns())
            addColumn(sm, M::ScriptColumn0 + d.slot);
    }
    QMenu *sums = menu.addMenu(QIcon::fromTheme(QStringLiteral("document-encrypt")), QStringLiteral("Checksums"));
    for (int c : {M::Crc32Column, M::Md5Column, M::Sha1Column, M::Sha256Column})
        addColumn(sums, c);
    QMenu *group = menu.addMenu(QStringLiteral("Group By"));
    using G = FileListModel::GroupBy;
    for (G g : FileListModel::allGroupings()) {
        if (g == G::Artist)
            group->addSection(QStringLiteral("From the Files"));
        QAction *a = group->addAction(FileListModel::groupByLabel(g));
        a->setCheckable(true);
        a->setChecked(groupBy() == g);
        connect(a, &QAction::triggered, this, [this, g]() { setGroupBy(g); });
    }
    group->addSeparator();
    QAction *collapse = group->addAction(QStringLiteral("Collapse All Groups"));
    QAction *expand = group->addAction(QStringLiteral("Expand All Groups"));
    collapse->setEnabled(groupBy() != G::None);
    expand->setEnabled(groupBy() != G::None);
    connect(collapse, &QAction::triggered, this, &FolderTab::collapseAllGroups);
    QAction *closeFolders = menu.addAction(QStringLiteral("Close All Opened Folders"));
    closeFolders->setEnabled(!m_children.isEmpty());
    connect(closeFolders, &QAction::triggered, this, &FolderTab::collapseAllFolders);
    connect(expand, &QAction::triggered, this, &FolderTab::expandAllGroups);
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

void FolderTab::collapseAllGroups()
{
    m_details->collapseAll();
}

void FolderTab::expandAllGroups()
{
    m_details->expandAll();
}

bool FolderTab::toggleCurrentGroup()
{
    if (groupBy() == FileListModel::GroupBy::None)
        return false;
    QModelIndex cur = m_selection->currentIndex();
    if (!cur.isValid() && m_proxy->rowCount() > 0)
        cur = m_proxy->index(0, 0);
    m_details->toggleGroup(cur);
    return true;
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

bool FolderTab::filterHides() const
{
    return m_filterInvert->isChecked();
}

FileSortProxy::FilterScope FolderTab::filterScope() const
{
    return static_cast<FileSortProxy::FilterScope>(m_filterScope->currentIndex());
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
    collapseAllFolders();
    m_model->clear(m_path);
    m_lister->setFlatMode(mode);
    updateLocationColumn();
    updateExpandable();
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
    else if (n == QLatin1String("mixed") || n == QLatin1String("ungrouped"))
        out = DirLister::FlatMode::Mixed;
    else if (n == QLatin1String("nofolders") || n == QLatin1String("mixednofolders") || n == QLatin1String("files"))
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
    // Mostly pictures: thumbnails. Any other folder keeps the view it had.
    if (files >= 4 && pictures * 10 >= files * 7) {
        m_modeBeforeAuto = m_mode;
        m_autoIcons = true;
        setViewMode(ViewMode::Thumbnails);
    }
}

void FolderTab::fitNameColumn()
{
    // Until it has been sized by hand, the name column takes the room left
    // over, but never less than enough to read a name: on a narrow display
    // the list scrolls sideways instead.
    if (!m_nameFills)
        return;
    QHeaderView *h = m_details->header();
    const int minName = m_details->fontMetrics().averageCharWidth() * 18;
    int others = 0;
    for (int c = 0; c < FileListModel::ColumnCount; ++c) {
        if (c != FileListModel::NameColumn && !h->isSectionHidden(c))
            others += h->sectionSize(c);
    }
    const int want = qMax(minName, m_details->viewport()->width() - others);
    if (h->sectionSize(FileListModel::NameColumn) == want)
        return;
    m_fittingName = true;
    h->resizeSection(FileListModel::NameColumn, want);
    m_fittingName = false;
}

bool FolderTab::nameColumnFills() const
{
    return m_nameFills;
}

QStyle *FolderTab::headerGripStyle()
{
    // A wider grab area at the column edges, for fingers on a touchscreen.
    class GripStyle : public QProxyStyle
    {
    public:
        int pixelMetric(PixelMetric m, const QStyleOption *o, const QWidget *w) const override
        {
            const int base = QProxyStyle::pixelMetric(m, o, w);
            if (m == PM_HeaderGripMargin)
                return qMax(base, Style::instance()->scaled(7));
            return base;
        }
    };
    static QPointer<QStyle> style;
    if (!style) {
        style = new GripStyle;
        style->setParent(qApp);
    }
    return style;
}

// ---- Print Folder ----------------------------------------------------------

QString FolderTab::exportListing(ExportFormat format, bool selectedOnly) const
{
    QHeaderView *h = m_details->header();
    QList<int> cols;
    for (int v = 0; v < h->count(); ++v) {
        const int c = h->logicalIndex(v);
        if (!h->isSectionHidden(c))
            cols << c;
    }
    const bool grouped = groupBy() != FileListModel::GroupBy::None;
    QList<int> rows;
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        if (!selectedOnly || m_selection->isRowSelected(r, QModelIndex()))
            rows << r;
    }
    auto cell = [this](int r, int c) { return m_proxy->index(r, c).data(Qt::DisplayRole).toString(); };
    auto group = [this](int r) { return m_proxy->index(r, 0).data(FileListModel::GroupLabelRole).toString(); };
    QStringList headers;
    for (int c : cols)
        headers << m_proxy->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();

    int folders = 0;
    int files = 0;
    for (int r : rows)
        (m_proxy->index(r, 0).data(FileListModel::IsDirRole).toBool() ? folders : files)++;
    const QString summary = QStringLiteral("%1 %2, %3 %4")
                                .arg(folders)
                                .arg(folders == 1 ? QStringLiteral("folder") : QStringLiteral("folders"))
                                .arg(files)
                                .arg(files == 1 ? QStringLiteral("file") : QStringLiteral("files"));

    if (format == ExportFormat::Csv) {
        auto q = [](QString v) {
            if (v.contains(QLatin1Char(',')) || v.contains(QLatin1Char('"')) || v.contains(QLatin1Char('\n'))) {
                v.replace(QLatin1Char('"'), QStringLiteral("\"\""));
                return QLatin1Char('"') + v + QLatin1Char('"');
            }
            return v;
        };
        QStringList lines;
        QStringList head = headers;
        if (grouped)
            head.prepend(QStringLiteral("Group"));
        for (QString &x : head)
            x = q(x);
        lines << head.join(QLatin1Char(','));
        for (int r : rows) {
            QStringList line;
            if (grouped)
                line << q(group(r));
            for (int c : cols)
                line << q(cell(r, c));
            lines << line.join(QLatin1Char(','));
        }
        return lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n");
    }

    if (format == ExportFormat::Html) {
        auto e = [](const QString &v) { return v.toHtmlEscaped(); };
        QString out = QStringLiteral(
                          "<!doctype html>\n<html><head><meta charset=\"utf-8\"><title>%1</title>\n<style>"
                          "body{font-family:sans-serif;margin:24px;color:#222}h1{font-size:18px}"
                          "table{border-collapse:collapse;font-size:13px}th,td{padding:3px 10px;text-align:left}"
                          "thead th{border-bottom:2px solid #888}tbody tr:nth-child(even){background:#f3f4f6}"
                          "th.group{background:#e3e8f0;padding-top:8px}p.sum{color:#666}"
                          "</style></head><body>\n<h1>%1</h1>\n<table>\n<thead><tr>")
                          .arg(e(m_path));
        for (const QString &x : headers)
            out += QStringLiteral("<th>%1</th>").arg(e(x));
        out += QStringLiteral("</tr></thead>\n<tbody>\n");
        QString lastGroup;
        bool first = true;
        for (int r : rows) {
            if (grouped && (first || group(r) != lastGroup)) {
                lastGroup = group(r);
                out += QStringLiteral("<tr><th class=\"group\" colspan=\"%1\">%2</th></tr>\n").arg(cols.size()).arg(e(lastGroup));
            }
            first = false;
            out += QStringLiteral("<tr>");
            for (int c : cols)
                out += QStringLiteral("<td>%1</td>").arg(e(cell(r, c)));
            out += QStringLiteral("</tr>\n");
        }
        out += QStringLiteral("</tbody>\n</table>\n<p class=\"sum\">%1</p>\n</body></html>\n").arg(e(summary));
        return out;
    }

    // Text: columns padded to their widest value.
    QList<int> widths;
    for (int i = 0; i < cols.size(); ++i) {
        int w = int(headers.at(i).size());
        for (int r : rows)
            w = qMax(w, int(cell(r, cols.at(i)).size()));
        widths << qMin(w, 60);
    }
    auto line = [&widths](const QStringList &values) {
        QString l;
        for (int i = 0; i < values.size(); ++i) {
            QString v = values.at(i);
            if (v.size() > widths.at(i))
                v = v.left(widths.at(i) - 1) + QChar(0x2026);
            l += i + 1 < values.size() ? v.leftJustified(widths.at(i) + 2) : v;
        }
        return l;
    };
    QStringList out{m_path, QString(), line(headers)};
    int total = 0;
    for (int w : widths)
        total += w + 2;
    out << QString(qMax(0, total - 2), QLatin1Char('-'));
    QString lastGroup;
    bool first = true;
    for (int r : rows) {
        if (grouped && (first || group(r) != lastGroup)) {
            lastGroup = group(r);
            if (!first)
                out << QString();
            out << QStringLiteral("[%1]").arg(lastGroup);
        }
        first = false;
        QStringList values;
        for (int c : cols)
            values << cell(r, c);
        out << line(values);
    }
    out << QString() << summary;
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

// ---- Find-as-you-type --------------------------------------------------------

bool FolderTab::typeAheadOpen() const
{
    return m_fayt && m_fayt->isVisible();
}

QString FolderTab::typeAheadMode() const
{
    const QString t = m_faytEdit->text();
    if (t.startsWith(QLatin1Char('>')))
        return QStringLiteral("command");
    if (t.startsWith(QLatin1Char('/')) || t.startsWith(QLatin1Char('~')))
        return QStringLiteral("go");
    if (t.contains(QLatin1Char('*')) || t.contains(QLatin1Char('?')))
        return QStringLiteral("select");
    return QStringLiteral("find");
}

void FolderTab::openTypeAhead(const QString &text)
{
    m_fayt->show();
    m_faytEdit->setText(text);
    m_faytEdit->setFocus(Qt::OtherFocusReason);
    m_faytEdit->end(false);
    updateTypeAhead();
}

void FolderTab::closeTypeAhead()
{
    if (!m_fayt->isVisible())
        return;
    const bool hadFocus = m_faytEdit->hasFocus();
    m_fayt->hide();
    {
        const QSignalBlocker block(m_faytEdit);
        m_faytEdit->clear();
    }
    if (hadFocus)
        focusView();
}

QList<QModelIndex> FolderTab::typeAheadMatches() const
{
    // Names that start with the text first, then names that contain it.
    const QString t = m_faytEdit->text();
    QList<QModelIndex> starts, contains;
    if (t.isEmpty())
        return {};
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        const QModelIndex idx = m_proxy->index(r, 0);
        const QString name = idx.data(Qt::DisplayRole).toString();
        if (name.startsWith(t, Qt::CaseInsensitive))
            starts << idx;
        else if (name.contains(t, Qt::CaseInsensitive))
            contains << idx;
    }
    return starts + contains;
}

void FolderTab::updateTypeAhead()
{
    if (m_faytEdit->text().isEmpty()) {
        closeTypeAhead();
        return;
    }
    const QString mode = typeAheadMode();
    QPalette pal = m_faytEdit->palette();
    pal.setColor(QPalette::Text, palette().color(QPalette::Text));
    if (mode == QLatin1String("command")) {
        m_faytLabel->setText(QStringLiteral("Command"));
    } else if (mode == QLatin1String("go")) {
        m_faytLabel->setText(QStringLiteral("Go to"));
    } else if (mode == QLatin1String("select")) {
        const int n = selectPattern(m_faytEdit->text());
        m_faytLabel->setText(QStringLiteral("Select (%1)").arg(n));
        if (n == 0)
            pal.setColor(QPalette::Text, QColor(0xc8, 0x38, 0x38));
    } else {
        const QList<QModelIndex> hits = typeAheadMatches();
        m_faytLabel->setText(hits.isEmpty() ? QStringLiteral("Find") : QStringLiteral("Find (%1)").arg(hits.size()));
        if (hits.isEmpty()) {
            pal.setColor(QPalette::Text, QColor(0xc8, 0x38, 0x38)); // nothing by that name
        } else {
            m_selection->setCurrentIndex(hits.first(), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            view()->scrollTo(hits.first());
        }
    }
    m_faytEdit->setPalette(pal);
}

void FolderTab::typeAheadStep(int delta)
{
    const QList<QModelIndex> hits = typeAheadMatches();
    if (hits.isEmpty())
        return;
    // In list order, from the one in focus.
    QList<QModelIndex> ordered = hits;
    std::sort(ordered.begin(), ordered.end(), [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });
    const int cur = m_selection->currentIndex().row();
    QModelIndex next;
    if (delta > 0) {
        for (const QModelIndex &i : std::as_const(ordered)) {
            if (i.row() > cur) {
                next = i;
                break;
            }
        }
        if (!next.isValid())
            next = ordered.first();
    } else {
        for (auto it = ordered.crbegin(); it != ordered.crend(); ++it) {
            if (it->row() < cur) {
                next = *it;
                break;
            }
        }
        if (!next.isValid())
            next = ordered.last();
    }
    m_selection->setCurrentIndex(next, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view()->scrollTo(next);
}

bool FolderTab::typeAheadKey(QKeyEvent *ke)
{
    const QString mode = typeAheadMode();
    switch (ke->key()) {
    case Qt::Key_Escape:
        closeTypeAhead();
        return true;
    case Qt::Key_Up:
        typeAheadStep(-1);
        return true;
    case Qt::Key_Down:
        typeAheadStep(1);
        return true;
    case Qt::Key_Backspace:
        if (m_faytEdit->text().size() <= 1) {
            closeTypeAhead();
            return true;
        }
        return false;
    case Qt::Key_Tab:
        if (mode == QLatin1String("command")) {
            const QStringList options = CommandCatalog::complete(m_faytEdit->text().mid(1));
            if (options.size() == 1)
                m_faytEdit->setText(QLatin1Char('>') + options.first());
            else if (!options.isEmpty())
                QToolTip::showText(m_faytEdit->mapToGlobal(QPoint(0, -m_faytEdit->height() * 2)),
                                   options.mid(0, 12).join(QLatin1Char('\n')), m_faytEdit);
        }
        return true;
    case Qt::Key_F:
        if (ke->modifiers() == Qt::ControlModifier) {
            // The text becomes a filter (wildcards as they are, plain text as "contains").
            QString text = m_faytEdit->text();
            if (!text.contains(QLatin1Char('*')) && !text.contains(QLatin1Char('?')))
                text = QLatin1Char('*') + text + QLatin1Char('*');
            closeTypeAhead();
            setFilter(text, false, FileSortProxy::FilterScope::Both);
            setFilterBarVisible(true);
            m_filterEdit->deselect();
            m_filterEdit->end(false);
            return true;
        }
        return false;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        const QString text = m_faytEdit->text();
        if (mode == QLatin1String("command")) {
            closeTypeAhead();
            Q_EMIT commandRequested(text.mid(1).trimmed());
        } else if (mode == QLatin1String("go")) {
            closeTypeAhead();
            Q_EMIT commandRequested(QStringLiteral("Go \"%1\"").arg(text.trimmed()));
        } else if (mode == QLatin1String("select")) {
            closeTypeAhead(); // the selection stays
        } else {
            const QModelIndex cur = m_selection->currentIndex();
            closeTypeAhead();
            if (cur.isValid())
                activateIndex(cur);
        }
        return true;
    }
    default:
        return false;
    }
}
