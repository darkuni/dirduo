#include "ui/FolderTree.h"
#include <QScrollBar>
#include <QPainter>
#include "core/Prefs.h"
#include "core/Servers.h"
#include "ui/Style.h"

#include <QApplication>

#include "core/AliasManager.h"
#include "core/Location.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QHeaderView>

#include <algorithm>
#include <functional>

// ---------------------------------------------------------------- model

struct FolderTreeModel::Node
{
    QString label;
    QString path;     // empty for a heading node ("Aliases")
    QString iconName; // top-level nodes only
    Node *parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;
    bool fetched = false;
    bool heading = false;

    int row() const
    {
        if (!parent)
            return 0;
        for (size_t i = 0; i < parent->children.size(); ++i) {
            if (parent->children[i].get() == this)
                return int(i);
        }
        return 0;
    }
};

FolderTreeModel::FolderTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
    , m_root(std::make_unique<Node>())
    , m_folderIcon(QIcon::fromTheme(QStringLiteral("folder")))
{
    m_root->fetched = true;
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &FolderTreeModel::onDirectoryChanged);
}

FolderTreeModel::~FolderTreeModel() = default;

void FolderTreeModel::setRoots(const QList<Root> &roots)
{
    beginResetModel();
    const QStringList watched = m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    m_roots = roots;
    m_root = std::make_unique<Node>();
    m_root->fetched = true;
    QHash<QString, Node *> headings;
    for (const Root &r : roots) {
        Node *parent = m_root.get();
        if (!r.group.isEmpty()) {
            Node *&h = headings[r.group];
            if (!h) {
                auto node = std::make_unique<Node>();
                node->label = r.group;
                node->heading = true;
                node->fetched = true;
                node->iconName = r.group == QLatin1String("Network") ? QStringLiteral("network-workgroup") : QStringLiteral("bookmarks");
                node->parent = m_root.get();
                h = node.get();
                m_root->children.push_back(std::move(node));
            }
            parent = h;
        }
        auto node = std::make_unique<Node>();
        node->label = r.label;
        node->path = Location::clean(r.path);
        node->iconName = r.icon;
        node->parent = parent;
        node->fetched = !Location::isLocal(r.path); // servers open in the list, not in the tree
        parent->children.push_back(std::move(node));
    }
    endResetModel();
}

void FolderTreeModel::setShowHidden(bool show)
{
    if (m_showHidden == show)
        return;
    m_showHidden = show;
    forEachFetched(m_root.get(), [this](Node *n) {
        if (!n->path.isEmpty())
            refresh(n);
    });
}

void FolderTreeModel::forEachFetched(Node *node, const std::function<void(Node *)> &fn)
{
    if (!node->fetched)
        return;
    // Collect first: fn may change the children.
    std::vector<Node *> kids;
    for (auto &c : node->children)
        kids.push_back(c.get());
    fn(node);
    for (Node *k : kids) {
        // Still a child after the refresh?
        bool alive = false;
        for (auto &c : node->children)
            alive = alive || c.get() == k;
        if (alive)
            forEachFetched(k, fn);
    }
}

FolderTreeModel::Node *FolderTreeModel::nodeOf(const QModelIndex &index) const
{
    return index.isValid() ? static_cast<Node *>(index.internalPointer()) : m_root.get();
}

QModelIndex FolderTreeModel::indexOf(Node *node) const
{
    if (!node || node == m_root.get())
        return {};
    return createIndex(node->row(), 0, node);
}

QString FolderTreeModel::pathOf(const QModelIndex &index) const
{
    return index.isValid() ? nodeOf(index)->path : QString();
}

QModelIndex FolderTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    Node *p = nodeOf(parent);
    if (column != 0 || row < 0 || row >= int(p->children.size()))
        return {};
    return createIndex(row, 0, p->children[size_t(row)].get());
}

QModelIndex FolderTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};
    return indexOf(nodeOf(child)->parent);
}

int FolderTreeModel::rowCount(const QModelIndex &parent) const
{
    return int(nodeOf(parent)->children.size());
}

int FolderTreeModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QVariant FolderTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    Node *n = nodeOf(index);
    switch (role) {
    case Qt::DisplayRole:
        return n->label;
    case Qt::DecorationRole:
        if (!n->iconName.isEmpty()) {
            const QIcon i = QIcon::fromTheme(n->iconName);
            if (!i.isNull())
                return i;
        }
        return m_folderIcon;
    case Qt::ToolTipRole:
        return n->path;
    default:
        return {};
    }
}

bool FolderTreeModel::hasChildren(const QModelIndex &parent) const
{
    Node *n = nodeOf(parent);
    if (!n->fetched)
        return true; // not read yet: assume so, like every file manager
    return !n->children.empty();
}

bool FolderTreeModel::canFetchMore(const QModelIndex &parent) const
{
    return !nodeOf(parent)->fetched;
}

void FolderTreeModel::fetchMore(const QModelIndex &parent)
{
    fetch(nodeOf(parent));
}

Qt::ItemFlags FolderTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    if (nodeOf(index)->heading)
        return Qt::ItemIsEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QStringList FolderTreeModel::listFolders(const QString &path) const
{
    QDir::Filters f = QDir::Dirs | QDir::NoDotAndDotDot;
    if (m_showHidden)
        f |= QDir::Hidden;
    QStringList names = QDir(path).entryList(f, QDir::NoSort);
    QCollator coll;
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(names.begin(), names.end(), [&coll](const QString &a, const QString &b) { return coll.compare(a, b) < 0; });
    return names;
}

void FolderTreeModel::fetch(Node *node)
{
    if (node->fetched)
        return;
    node->fetched = true;
    const QStringList names = listFolders(node->path);
    const QModelIndex idx = indexOf(node);
    if (!names.isEmpty()) {
        beginInsertRows(idx, 0, int(names.size()) - 1);
        for (const QString &name : names) {
            auto child = std::make_unique<Node>();
            child->label = name;
            child->path = Location::join(node->path, name);
            child->parent = node;
            node->children.push_back(std::move(child));
        }
        endInsertRows();
    } else {
        // No subfolders after all: the expand arrow goes away.
        Q_EMIT dataChanged(idx, idx);
    }
    if (!node->path.isEmpty())
        m_watcher.addPath(node->path);
}

void FolderTreeModel::refresh(Node *node)
{
    if (!node->fetched)
        return;
    const QStringList names = listFolders(node->path);
    const QModelIndex idx = indexOf(node);
    // Gone: remove from the bottom up so rows stay valid.
    for (int r = int(node->children.size()) - 1; r >= 0; --r) {
        if (!names.contains(node->children[size_t(r)]->label)) {
            beginRemoveRows(idx, r, r);
            Node *dead = node->children[size_t(r)].get();
            forEachFetched(dead, [this](Node *n) {
                if (!n->path.isEmpty())
                    m_watcher.removePath(n->path);
            });
            node->children.erase(node->children.begin() + r);
            endRemoveRows();
        }
    }
    // New: insert in sorted position (names is sorted).
    int at = 0;
    for (const QString &name : names) {
        if (at < int(node->children.size()) && node->children[size_t(at)]->label == name) {
            ++at;
            continue;
        }
        bool exists = false;
        for (auto &c : node->children)
            exists = exists || c->label == name;
        if (exists) {
            ++at;
            continue;
        }
        beginInsertRows(idx, at, at);
        auto child = std::make_unique<Node>();
        child->label = name;
        child->path = Location::join(node->path, name);
        child->parent = node;
        node->children.insert(node->children.begin() + at, std::move(child));
        endInsertRows();
        ++at;
    }
}

void FolderTreeModel::onDirectoryChanged(const QString &path)
{
    const QString p = Location::clean(path);
    forEachFetched(m_root.get(), [this, &p](Node *n) {
        if (n->path == p)
            refresh(n);
    });
}

QModelIndex FolderTreeModel::indexForPath(const QString &rawPath)
{
    const QString path = Location::clean(rawPath);
    // The closest root wins: Home over Root, the SD card over /run/media.
    Node *best = nullptr;
    std::function<void(Node *)> scan = [&](Node *n) {
        for (auto &c : n->children) {
            if (c->heading) {
                scan(c.get());
                continue;
            }
            if (Location::isSameOrInside(path, c->path) && (!best || c->path.size() > best->path.size()))
                best = c.get();
        }
    };
    scan(m_root.get());
    if (!best)
        return {};
    Node *node = best;
    const QString rest = path.mid(node->path.size());
    const QStringList parts = rest.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        fetch(node);
        Node *next = nullptr;
        for (auto &c : node->children) {
            if (c->label == part) {
                next = c.get();
                break;
            }
        }
        if (!next)
            break; // a hidden folder while hidden ones are off: stop at its parent
        node = next;
    }
    return indexOf(node);
}

// ---------------------------------------------------------------- view

FolderTree::FolderTree(AliasManager *aliases, QWidget *parent)
    : QTreeView(parent)
    , m_aliases(aliases)
    , m_model(new FolderTreeModel(this))
{
    connect(Style::instance(), &Style::changed, this, [this]() {
        setZoom(m_zoom); // a new text size under the zoom
        if (m_autoColor) {
            m_pathColor = Style::instance()->theme().sourceAccent; // the shared tree: the source's color
            viewport()->update();
        }
    });
    setModel(m_model);
    header()->hide();
    // One column as wide as the longest name shown (at least the view), with a
    // scroll bar across, so deep folders keep their names instead of being cut
    // to nothing; the current one is scrolled into view (as Opus's tree).
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Fixed);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setTextElideMode(Qt::ElideNone);
    m_fit.setSingleShot(true);
    m_fit.setInterval(0);
    connect(&m_fit, &QTimer::timeout, this, &FolderTree::fitColumn);
    connect(this, &QTreeView::expanded, &m_fit, qOverload<>(&QTimer::start));
    connect(this, &QTreeView::collapsed, &m_fit, qOverload<>(&QTimer::start));
    connect(m_model, &QAbstractItemModel::rowsInserted, &m_fit, qOverload<>(&QTimer::start));
    connect(m_model, &QAbstractItemModel::rowsRemoved, &m_fit, qOverload<>(&QTimer::start));
    connect(m_model, &QAbstractItemModel::modelReset, &m_fit, qOverload<>(&QTimer::start));
    connect(m_model, &QAbstractItemModel::dataChanged, &m_fit, qOverload<>(&QTimer::start));
    setUniformRowHeights(true);
    setAnimated(false);
    setIndentation(Style::instance()->scaled(18));
    setExpandsOnDoubleClick(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setMinimumWidth(120);
    setFrameShape(QFrame::NoFrame);

    // The path line runs through many rows: repaint them all when it moves.
    connect(selectionModel(), &QItemSelectionModel::currentChanged, viewport(), qOverload<>(&QWidget::update));
    connect(Prefs::instance(), &Prefs::changed, viewport(), qOverload<>(&QWidget::update));
    m_pathColor = Style::instance()->theme().sourceAccent;
    // Click, Enter or arrow keys: the display follows.
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &cur) {
        if (m_syncing || !cur.isValid())
            return;
        const QString path = m_model->pathOf(cur);
        if (path.isEmpty() || path == m_path)
            return;
        m_path = path;
        Q_EMIT folderActivated(path);
    });

    m_poll.setInterval(3000);
    connect(&m_poll, &QTimer::timeout, this, &FolderTree::rescan);
    rescan();
    m_poll.start();
    setPath(QDir::homePath());
    // Home starts open.
    if (m_model->rowCount() > 0)
        expand(m_model->index(0, 0));
}

void FolderTree::rescan()
{
    rebuild(DriveList::scan());
}

void FolderTree::setDrivesForTesting(const QList<DriveEntry> &drives)
{
    m_poll.stop();
    rebuild(drives);
}

void FolderTree::rebuild(const QList<DriveEntry> &drives)
{
    QList<FolderTreeModel::Root> roots;
    roots << FolderTreeModel::Root{QStringLiteral("Home"), QDir::homePath(), QStringLiteral("user-home"), {}};
    for (const DriveEntry &d : drives) {
        if (d.kind == DriveEntry::Kind::Home)
            continue;
        roots << FolderTreeModel::Root{d.kind == DriveEntry::Kind::Root ? QStringLiteral("Root") : d.name, d.path,
                                       DriveList::iconName(d.kind), {}};
    }
    if (m_aliases) {
        for (const QString &name : m_aliases->names()) {
            if (!m_aliases->isUserAlias(name))
                continue;
            const QString target = m_aliases->aliasPath(name);
            if (target.isEmpty() || !Location::isLocal(target))
                continue;
            roots << FolderTreeModel::Root{QLatin1Char('/') + name, target, QStringLiteral("folder-bookmark"),
                                           QStringLiteral("Aliases")};
        }
    }
    // The network: Windows and Samba shares, and the saved servers.
    roots << FolderTreeModel::Root{QStringLiteral("Windows Shares"), QStringLiteral("smb:/"), QStringLiteral("network-workgroup"),
                                   QStringLiteral("Network")};
    for (const Server &sv : Servers::instance()->items())
        roots << FolderTreeModel::Root{sv.name, sv.url, QStringLiteral("network-server"), QStringLiteral("Network")};
    auto same = [](const QList<FolderTreeModel::Root> &a, const QList<FolderTreeModel::Root> &b) {
        if (a.size() != b.size())
            return false;
        for (int i = 0; i < a.size(); ++i) {
            if (a[i].label != b[i].label || a[i].path != b[i].path || a[i].group != b[i].group)
                return false;
        }
        return true;
    };
    if (same(roots, m_lastRoots))
        return;
    m_lastRoots = roots;
    m_model->setRoots(roots);
    if (m_model->rowCount() > 0)
        expand(m_model->index(0, 0));
    reveal();
}

void FolderTree::setShowHidden(bool show)
{
    m_model->setShowHidden(show);
    reveal();
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
    if (m_path.isEmpty())
        return;
    const QModelIndex idx = m_model->indexForPath(m_path);
    if (!idx.isValid())
        return;
    m_syncing = true;
    for (QModelIndex p = idx.parent(); p.isValid(); p = p.parent())
        expand(p);
    selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
    fitColumn();
    scrollTo(idx, QAbstractItemView::EnsureVisible);
    showAcross(idx);
    m_syncing = false;
}

void FolderTree::fitColumn()
{
    const int want = qMax(sizeHintForColumn(0), viewport()->width());
    if (header()->sectionSize(0) != want)
        header()->resizeSection(0, want);
}

// Scrolls across so the folder's whole name shows, as little as it takes.
void FolderTree::showAcross(const QModelIndex &idx)
{
    QScrollBar *bar = horizontalScrollBar();
    const QRect r = visualRect(idx);
    if (!r.isValid())
        return;
    const int left = r.left() + bar->value(); // in column coordinates
    const int textWidth = sizeHintForIndex(idx).width();
    // Only as far as it takes to show the whole name, never past its start.
    const int need = left + textWidth + 6 - viewport()->width();
    bar->setValue(qBound(0, need, qMax(0, left - indentation())));
}

void FolderTree::resizeEvent(QResizeEvent *event)
{
    QTreeView::resizeEvent(event);
    m_fit.start();
}

void FolderTree::setZoom(int percent)
{
    m_zoom = percent;
    QFont f = QApplication::font();
    if (percent != 100) {
        if (f.pointSizeF() > 0)
            f.setPointSizeF(f.pointSizeF() * percent / 100.0);
        else
            f.setPixelSize(qMax(6, int(f.pixelSize() * percent / 100.0 + 0.5)));
    }
    setFont(f);
    const int px = qMax(8, int(Style::instance()->scaled(16) * percent / 100.0 + 0.5));
    setIconSize(QSize(px, px));
    setIndentation(qMax(12, int(Style::instance()->scaled(18) * percent / 100.0 + 0.5)));
}

void FolderTree::setPathColor(const QColor &c)
{
    m_autoColor = false;
    if (c == m_pathColor)
        return;
    m_pathColor = c;
    viewport()->update();
}

QList<QModelIndex> FolderTree::pathIndexes() const
{
    QList<QModelIndex> out;
    for (QModelIndex i = currentIndex(); i.isValid(); i = i.parent())
        out.prepend(i);
    return out;
}

// Opus-style branches: dotted lines joining each folder to its parent, a box
// with + or - where a folder can open, and the way down from the top to the
// current folder drawn solid in the display's color.
void FolderTree::drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const
{
    const Prefs::Values &prefs = Prefs::instance()->values();
    if (!prefs.treeLines && !prefs.treePath) {
        QTreeView::drawBranches(painter, rect, index);
        return;
    }
    const QList<QModelIndex> path = prefs.treePath ? pathIndexes() : QList<QModelIndex>();
    // The folder on the path at a depth, if the path goes that deep.
    auto onPath = [&path](int depth) -> QModelIndex { return depth < path.size() ? path.at(depth) : QModelIndex(); };
    // Ancestors of this row, top first, ending with the row itself.
    QList<QModelIndex> chain;
    for (QModelIndex i = index; i.isValid(); i = i.parent())
        chain.prepend(i);
    const int depth = int(chain.size()) - 1;
    const int indent = indentation();
    const int cy = rect.center().y();

    QColor lineColor = palette().color(QPalette::Text);
    lineColor.setAlphaF(0.45);
    QPen dotted(lineColor, 1, Qt::DotLine);
    const int pathWidth = qMax(2, (indent + 2) / 6);
    QPen solid(m_pathColor.isValid() ? m_pathColor : palette().color(QPalette::Highlight), pathWidth);
    solid.setCapStyle(Qt::FlatCap);
    painter->save();

    // Whether the path line passes this row in column c, and how.
    // Column c holds the line joining the children of the folder at depth c-1.
    for (int c = 0; c <= depth; ++c) {
        const QModelIndex here = chain.at(c); // this row's folder at depth c
        const int x = rect.right() - (depth - c) * indent - indent / 2;
        const QAbstractItemModel *m = model();
        const bool hasNext = here.row() + 1 < m->rowCount(here.parent());
        const bool first = c == 0 && here.row() == 0;
        // The path at this column: the parent must be on it (the top level always is).
        const bool parentOnPath = c == 0 || (c - 1 < path.size() && path.at(c - 1) == chain.at(c - 1));
        const QModelIndex next = parentOnPath ? onPath(c) : QModelIndex();
        if (c < depth) {
            // A line passing by, for a folder further down at this level.
            if (prefs.treeLines && hasNext) {
                painter->setPen(dotted);
                painter->drawLine(x, rect.top(), x, rect.bottom());
            }
            if (next.isValid() && here.row() < next.row()) {
                painter->setPen(solid);
                painter->drawLine(x, rect.top(), x, rect.bottom() + 1);
            }
            continue;
        }
        // This row's own column: the elbow into it, and its box.
        if (prefs.treeLines) {
            painter->setPen(dotted);
            if (!first)
                painter->drawLine(x, rect.top(), x, cy);
            if (hasNext)
                painter->drawLine(x, cy, x, rect.bottom());
            painter->drawLine(x, cy, rect.right(), cy);
        }
        if (next.isValid()) {
            painter->setPen(solid);
            if (here == next) {
                if (!first)
                    painter->drawLine(x, rect.top(), x, cy);
                painter->drawLine(x - pathWidth / 2, cy, rect.right() + 1, cy);
            } else if (here.row() < next.row()) {
                painter->drawLine(x, first ? cy : rect.top(), x, rect.bottom() + 1);
            }
        }
        if (m->hasChildren(here)) {
            const int b = qMin(qMax(7, Style::instance()->scaled(9) | 1), qMax(7, (rect.height() / 2) | 1));
            const QRect box(x - b / 2, cy - b / 2, b, b);
            QColor frame = palette().color(QPalette::Text);
            frame.setAlphaF(0.6);
            painter->setPen(QPen(frame, 1));
            painter->setBrush(palette().color(QPalette::Base));
            painter->drawRect(box);
            painter->drawLine(box.left() + 2, cy, box.right() - 2, cy);
            if (!isExpanded(here))
                painter->drawLine(x, box.top() + 2, x, box.bottom() - 2);
        }
    }
    painter->restore();
}
