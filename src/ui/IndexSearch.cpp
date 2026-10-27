#include "ui/IndexSearch.h"

#include "core/Collections.h"
#include "core/FileOps.h"
#include "core/Location.h"
#include "ui/ListerWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {
QPointer<IndexSearch> g_window;
constexpr int kShown = 5000;
}

void IndexResultsModel::setHits(const QList<FileIndex::Hit> &hits)
{
    beginResetModel();
    m_hits = hits;
    endResetModel();
}

QVariant IndexResultsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_hits.size())
        return {};
    const FileIndex::Hit &h = m_hits.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0:
            return h.name;
        case 1:
            return Location::display(h.folder);
        case 2:
            return h.isDir ? QString() : FileOps::formatSize(h.size);
        case 3:
            return QLocale().toString(h.modified, QLocale::ShortFormat);
        }
    }
    if (role == Qt::DecorationRole && index.column() == 0) {
        static QFileIconProvider icons;
        static const QIcon folder = QIcon::fromTheme(QStringLiteral("folder"));
        return h.isDir ? folder : icons.icon(QFileIconProvider::File);
    }
    if (role == Qt::ToolTipRole)
        return h.path();
    if (role == Qt::TextAlignmentRole && index.column() == 2)
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

QVariant IndexResultsModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    static const QStringList h{QStringLiteral("Name"), QStringLiteral("Folder"), QStringLiteral("Size"), QStringLiteral("Modified")};
    return h.value(section);
}

Qt::ItemFlags IndexResultsModel::flags(const QModelIndex &index) const
{
    return QAbstractTableModel::flags(index) | Qt::ItemIsDragEnabled;
}

QStringList IndexResultsModel::mimeTypes() const
{
    return {QStringLiteral("text/uri-list")};
}

QMimeData *IndexResultsModel::mimeData(const QModelIndexList &indexes) const
{
    QList<QUrl> urls;
    QSet<int> rows;
    for (const QModelIndex &i : indexes)
        if (!rows.contains(i.row())) {
            rows.insert(i.row());
            urls << QUrl::fromLocalFile(m_hits.at(i.row()).path());
        }
    auto *m = new QMimeData;
    m->setUrls(urls);
    return m;
}

IndexSearch *IndexSearch::open(ListerWindow *lister, const QString &query)
{
    if (!g_window)
        g_window = new IndexSearch(lister);
    if (lister)
        g_window->m_lister = lister;
    if (!query.isEmpty())
        g_window->setQuery(query);
    g_window->show();
    g_window->raise();
    g_window->activateWindow();
    g_window->m_edit->setFocus();
    g_window->m_edit->selectAll();
    return g_window;
}

IndexSearch *IndexSearch::current()
{
    return g_window;
}

IndexSearch::IndexSearch(ListerWindow *lister)
    : QWidget(nullptr, Qt::Window)
    , m_lister(lister)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(QStringLiteral("indexSearch"));
    setWindowTitle(QStringLiteral("Index Search - Directory Duo"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("edit-find")));
    auto *layout = new QVBoxLayout(this);
    m_edit = new QLineEdit(this);
    m_edit->setObjectName(QStringLiteral("index-query"));
    m_edit->setPlaceholderText(QStringLiteral("Type part of a name: mario kart, *.d64, ext:mp3;flac, folder:, roms/snes"));
    m_edit->setClearButtonEnabled(true);
    QFont big = m_edit->font();
    big.setPointSizeF(big.pointSizeF() * 1.25);
    m_edit->setFont(big);
    m_edit->installEventFilter(this);
    layout->addWidget(m_edit);
    m_model = new IndexResultsModel(this);
    m_view = new QTreeView(this);
    m_view->setObjectName(QStringLiteral("index-results"));
    m_view->setModel(m_model);
    m_view->setRootIsDecorated(false);
    m_view->setUniformRowHeights(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setDragEnabled(true);
    m_view->setDragDropMode(QAbstractItemView::DragOnly);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->header()->resizeSection(0, 320);
    m_view->header()->resizeSection(1, 360);
    m_view->header()->resizeSection(2, 80);
    m_view->installEventFilter(this);
    layout->addWidget(m_view, 1);
    auto *row = new QHBoxLayout;
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("index-status"));
    auto *toTab = new QPushButton(QIcon::fromTheme(QStringLiteral("tab-new")), QStringLiteral("Show All in a Tab"), this);
    toTab->setToolTip(QStringLiteral("Every match as a collection in the lister, to copy, delete or tag"));
    auto *rebuild = new QToolButton(this);
    rebuild->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    rebuild->setToolTip(QStringLiteral("Build the index again now"));
    auto *settings = new QToolButton(this);
    settings->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    settings->setToolTip(QStringLiteral("Index settings: which folders, how often"));
    row->addWidget(m_status, 1);
    row->addWidget(toTab);
    row->addWidget(rebuild);
    row->addWidget(settings);
    layout->addLayout(row);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(90);
    connect(&m_debounce, &QTimer::timeout, this, &IndexSearch::runSearch);
    connect(m_edit, &QLineEdit::textChanged, this, [this]() { m_debounce.start(); });
    connect(&m_watcher, &QFutureWatcher<FileIndex::Result>::finished, this, [this]() {
        const FileIndex::Result r = m_watcher.result();
        m_running.clear();
        if (m_again) {
            m_again = false;
            runSearch();
            return;
        }
        m_model->setHits(r.hits);
        m_total = r.total;
        m_truncated = r.truncated;
        if (m_model->rowCount() > 0 && !m_view->currentIndex().isValid())
            m_view->setCurrentIndex(m_model->index(0, 0));
        updateStatus();
    });
    connect(m_view, &QTreeView::activated, this, [this](const QModelIndex &i) { goToRow(i.row()); });
    connect(m_view, &QTreeView::customContextMenuRequested, this, &IndexSearch::contextMenu);
    connect(toTab, &QPushButton::clicked, this, &IndexSearch::showAllInTab);
    connect(rebuild, &QToolButton::clicked, FileIndex::instance(), &FileIndex::rebuild);
    connect(settings, &QToolButton::clicked, this, [this]() {
        if (m_lister)
            m_lister->runCommand(QStringLiteral("Settings PAGE=index"));
    });
    connect(FileIndex::instance(), &FileIndex::changed, this, [this]() {
        if (!m_edit->text().trimmed().isEmpty())
            m_debounce.start();
        updateStatus();
    });
    connect(FileIndex::instance(), &FileIndex::buildingChanged, this, &IndexSearch::updateStatus);
    if (QScreen *s = lister ? lister->screen() : screen())
        resize(qMin(1000, s->availableSize().width() * 3 / 4), qMin(640, s->availableSize().height() * 3 / 4));
    updateStatus();
}

void IndexSearch::setQuery(const QString &q)
{
    m_edit->setText(q);
    m_debounce.stop();
    runSearch();
}

QString IndexSearch::query() const
{
    return m_edit->text();
}

void IndexSearch::runSearch()
{
    const QString q = m_edit->text().trimmed();
    if (m_watcher.isRunning()) {
        if (m_cancel)
            *m_cancel = true; // stale: stop it and search again
        m_again = true;
        return;
    }
    if (q.isEmpty()) {
        m_model->setHits({});
        m_total = 0;
        m_truncated = false;
        updateStatus();
        return;
    }
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    auto cancel = m_cancel;
    m_running = q;
    m_watcher.setFuture(QtConcurrent::run([q, cancel]() { return FileIndex::instance()->search(q, kShown, cancel.get()); }));
}

bool IndexSearch::waitResults(int ms)
{
    QElapsedTimer t;
    t.start();
    while ((m_debounce.isActive() || m_watcher.isRunning() || m_again) && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QCoreApplication::processEvents();
    return !m_watcher.isRunning();
}

QString IndexSearch::statusText() const
{
    return m_status->text();
}

void IndexSearch::updateStatus()
{
    FileIndex *fi = FileIndex::instance();
    QStringList bits;
    if (!fi->settings().enabled) {
        m_status->setText(QStringLiteral("The index is off (Preferences > Index Search)."));
        return;
    }
    if (!m_edit->text().trimmed().isEmpty()) {
        const QLocale l;
        bits << (m_truncated ? QStringLiteral("%1 of %2 shown").arg(l.toString(m_model->rowCount()), l.toString(m_total))
                             : (m_total == 1 ? QStringLiteral("1 match") : QStringLiteral("%1 matches").arg(l.toString(m_total))));
    }
    if (fi->isBuilding())
        bits << (fi->isReady() ? QStringLiteral("updating the index...") : QStringLiteral("building the index..."));
    else if (fi->isReady()) {
        const qint64 mins = fi->builtAt().secsTo(QDateTime::currentDateTime()) / 60;
        bits << QStringLiteral("%1 items, indexed %2").arg(QLocale().toString(fi->count()),
                                                           mins < 1 ? QStringLiteral("just now")
                                                           : mins < 120 ? QStringLiteral("%1 min ago").arg(mins)
                                                                        : QStringLiteral("%1 h ago").arg(mins / 60));
    }
    m_status->setText(bits.join(QStringLiteral(" - ")));
}

void IndexSearch::goToRow(int row)
{
    if (row < 0 || row >= m_model->rowCount() || !m_lister)
        return;
    const FileIndex::Hit &h = m_model->hit(row);
    QString p = h.path();
    p.remove(QLatin1Char('"'));
    // Go to a file opens its folder with it selected.
    m_lister->runCommand(QStringLiteral("Go \"%1\"").arg(p));
    m_lister->raise();
    m_lister->activateWindow();
}

void IndexSearch::openRow(int row)
{
    if (row < 0 || row >= m_model->rowCount() || !m_lister)
        return;
    const FileIndex::Hit &h = m_model->hit(row);
    if (h.isDir)
        goToRow(row);
    else
        m_lister->openFile(h.path());
}

bool IndexSearch::showAllInTab()
{
    if (!m_lister || m_edit->text().trimmed().isEmpty())
        return false;
    // Every match, not only those shown.
    const FileIndex::Result r = FileIndex::instance()->search(m_edit->text(), 100000);
    QStringList paths;
    for (const FileIndex::Hit &h : r.hits)
        paths << h.path();
    const QString name = QStringLiteral("Index Search Results");
    Collections::instance()->setItems(name, paths);
    return m_lister->runCommand(QStringLiteral("Go \"%1\" NEWTAB").arg(Collections::location(name)));
}

void IndexSearch::contextMenu(const QPoint &pos)
{
    const QModelIndex i = m_view->indexAt(pos);
    if (!i.isValid())
        return;
    const int row = i.row();
    const FileIndex::Hit h = m_model->hit(row);
    QMenu menu(this);
    menu.addAction(QIcon::fromTheme(QStringLiteral("go-jump")), QStringLiteral("Go to It"), this, [this, row]() { goToRow(row); });
    if (!h.isDir)
        menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), QStringLiteral("Open"), this, [this, row]() { openRow(row); });
    menu.addAction(QIcon::fromTheme(QStringLiteral("tab-new")), QStringLiteral("Open Its Folder in a New Tab"), this, [this, h]() {
        if (!m_lister)
            return;
        QString f = h.isDir ? h.path() : h.folder;
        f.remove(QLatin1Char('"'));
        m_lister->runCommand(QStringLiteral("Go \"%1\" NEWTAB").arg(f));
    });
    menu.addSeparator();
    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), QStringLiteral("Copy Path"), this, [h]() {
        QApplication::clipboard()->setText(h.path());
    });
    menu.addAction(QIcon::fromTheme(QStringLiteral("tab-new")), QStringLiteral("Show All in a Tab"), this, &IndexSearch::showAllInTab);
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

bool IndexSearch::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(event);
        if (watched == m_edit && (k->key() == Qt::Key_Down || k->key() == Qt::Key_PageDown)) {
            m_view->setFocus();
            if (!m_view->currentIndex().isValid() && m_model->rowCount() > 0)
                m_view->setCurrentIndex(m_model->index(0, 0));
            return true;
        }
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
            const int row = m_view->currentIndex().isValid() ? m_view->currentIndex().row() : 0;
            if (k->modifiers() & Qt::ControlModifier)
                openRow(row);
            else
                goToRow(row);
            return true;
        }
        if (watched == m_view && k->key() == Qt::Key_Up && m_view->currentIndex().row() == 0) {
            m_edit->setFocus();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void IndexSearch::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}
