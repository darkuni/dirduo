#include "ui/FileDisplay.h"
#include <QLocale>
#include "core/Location.h"
#include "core/FileOps.h"

#include "ui/BreadcrumbBar.h"
#include "ui/FolderTree.h"
#include "ui/Style.h"

#include <QSplitter>
#include <QMouseEvent>
#include <QMimeData>
#include <QMenu>
#include <QDrag>
#include <QDir>
#include <QApplication>
#include "ui/Glyphs.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QToolButton *navButton(const QString &icon, Qt::ArrowType fallback, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    const QIcon i = QIcon::fromTheme(icon);
    if (i.isNull())
        b->setArrowType(fallback);
    else
        b->setIcon(i);
    b->setAutoRaise(true);
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

// The current folder's icon in the location bar: drag it to copy, move or
// link the folder itself, as with an item in the list.
class FolderHandle : public QLabel
{
public:
    explicit FolderHandle(QWidget *parent)
        : QLabel(parent)
    {
        setCursor(Qt::OpenHandCursor);
        setToolTip(QStringLiteral("This folder: drag it somewhere to copy or move it"));
    }
    QString path;

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        m_start = e->position().toPoint();
        QLabel::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!(e->buttons() & Qt::LeftButton) || path.isEmpty()
            || (e->position().toPoint() - m_start).manhattanLength() < QApplication::startDragDistance())
            return;
        auto *drag = new QDrag(this);
        auto *mime = new QMimeData;
        mime->setUrls({Location::toUrl(path)});
        drag->setMimeData(mime);
        if (!pixmap().isNull())
            drag->setPixmap(pixmap());
        drag->exec(Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, Qt::CopyAction);
    }

private:
    QPoint m_start;
};

QToolButton *glyphButton(const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setAutoRaise(true);
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

} // namespace

FileDisplay::FileDisplay(AliasManager &aliases, QWidget *parent)
    : QWidget(parent)
    , m_aliases(aliases)
    , m_accent(new QWidget(this))
    , m_body(new QSplitter(Qt::Horizontal, this))
    , m_badge(new QLabel(this))
    , m_back(navButton(QStringLiteral("go-previous"), Qt::LeftArrow, QStringLiteral("Back (Alt+Left)"), this))
    , m_forward(navButton(QStringLiteral("go-next"), Qt::RightArrow, QStringLiteral("Forward (Alt+Right)"), this))
    , m_up(navButton(QStringLiteral("go-up"), Qt::UpArrow, QStringLiteral("Up (Backspace)"), this))
    , m_crumbs(new BreadcrumbBar(aliases, this))
    , m_tabs(new QTabBar(this))
    , m_stack(new QStackedWidget(this))
    , m_status(new QLabel(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_accent->setFixedHeight(3);
    m_accent->setAutoFillBackground(true);
    layout->addWidget(m_accent);

    auto *header = new QWidget(this);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(4, 2, 4, 2);
    hl->setSpacing(2);
    // Laid out as in Opus: back, forward, up, refresh, home, favorites, the
    // folder's icon, the path, and recent folders at the end. The bar takes
    // the display's role color (source, destination, locked destination).
    m_header = header;
    header->setAutoFillBackground(true);
    m_badge->setAlignment(Qt::AlignCenter);
    m_badge->setMinimumWidth(64);
    m_badge->setContentsMargins(6, 1, 6, 1);
    m_badge->hide(); // the bar's color says it now; kept for the locked mark
    m_refresh = glyphButton(QStringLiteral("Refresh (F5)"), header);
    m_home = glyphButton(QStringLiteral("Home"), header);
    m_star = glyphButton(QStringLiteral("Favorites"), header);
    m_star->setPopupMode(QToolButton::InstantPopup);
    m_star->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));
    auto *favMenu = new QMenu(m_star);
    connect(favMenu, &QMenu::aboutToShow, this, [this, favMenu]() {
        Q_EMIT activated(this); // a pick opens here
        favMenu->clear();
        if (m_menuProvider)
            m_menuProvider(favMenu, QStringLiteral("favorites"));
    });
    m_star->setMenu(favMenu);
    auto *handle = new FolderHandle(header);
    m_folderIcon = handle;
    m_recent = glyphButton(QStringLiteral("Recent folders"), header);
    m_recent->setPopupMode(QToolButton::InstantPopup);
    m_recent->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));
    auto *recentMenu = new QMenu(m_recent);
    connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu]() { fillRecentMenu(recentMenu); });
    m_recent->setMenu(recentMenu);
    for (QToolButton *b : {m_back, m_forward, m_up})
        b->setArrowType(Qt::NoArrow);
    hl->addWidget(m_badge);
    hl->addWidget(m_back);
    hl->addWidget(m_forward);
    hl->addWidget(m_up);
    hl->addWidget(m_refresh);
    hl->addWidget(m_home);
    hl->addWidget(m_star);
    hl->addSpacing(2);
    hl->addWidget(m_folderIcon);
    hl->addWidget(m_crumbs, 1);
    hl->addWidget(m_recent);
    // With two displays: this folder in the other one, and swap the two.
    m_copyPath = glyphButton(QStringLiteral("Open this folder in the other display"), header);
    m_swapPaths = glyphButton(QStringLiteral("Swap the folders of the two displays"), header);
    connect(m_copyPath, &QToolButton::clicked, this, [this]() { Q_EMIT copyPathRequested(this); });
    connect(m_swapPaths, &QToolButton::clicked, this, [this]() { Q_EMIT commandRequested(this, QStringLiteral("Go SWAP")); });
    hl->addSpacing(4);
    hl->addWidget(m_copyPath);
    hl->addWidget(m_swapPaths);
    layout->addWidget(header);

    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setExpanding(false);
    m_tabs->setAutoHide(true);
    m_tabs->setElideMode(Qt::ElideMiddle);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    // Tree (when shown) beside the tabs and the folder.
    auto *panel = new QWidget(m_body);
    auto *pl = new QVBoxLayout(panel);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(0);
    m_tabs->setParent(panel);
    m_stack->setParent(panel);
    pl->addWidget(m_tabs);
    pl->addWidget(m_stack, 1);
    m_body->addWidget(panel);
    m_body->setChildrenCollapsible(false);
    layout->addWidget(m_body, 1);

    m_status->setContentsMargins(6, 2, 6, 2);
    m_status->setTextInteractionFlags(Qt::NoTextInteraction);
    QFont small = m_status->font();
    small.setPointSizeF(small.pointSizeF() * 0.92);
    m_status->setFont(small);
    // Free space on the drive, at the right of the status line.
    m_free = new QLabel(this);
    m_free->setContentsMargins(6, 2, 6, 2);
    m_free->setFont(small);
    m_free->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->setSpacing(0);
    statusRow->addWidget(m_status, 1);
    statusRow->addWidget(m_free);
    layout->addLayout(statusRow);

    connect(m_back, &QToolButton::clicked, this, [this]() {
        if (auto *t = currentTab())
            t->back();
        Q_EMIT activated(this);
    });
    connect(m_forward, &QToolButton::clicked, this, [this]() {
        if (auto *t = currentTab())
            t->forward();
        Q_EMIT activated(this);
    });
    connect(m_up, &QToolButton::clicked, this, [this]() {
        if (auto *t = currentTab())
            t->up();
        Q_EMIT activated(this);
    });
    connect(m_refresh, &QToolButton::clicked, this, [this]() {
        if (auto *t = currentTab())
            t->refresh();
        Q_EMIT activated(this);
    });
    connect(m_home, &QToolButton::clicked, this, [this]() {
        Q_EMIT activated(this);
        if (auto *t = currentTab())
            t->navigate(QDir::homePath());
    });
    connect(m_crumbs, &BreadcrumbBar::navigateRequested, this, [this](const QString &input) {
        Q_EMIT activated(this);
        Q_EMIT navigateRequested(this, input);
    });
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (index < 0)
            return;
        auto *t = qobject_cast<FolderTab *>(m_tabs->tabData(index).value<QObject *>());
        if (t)
            m_stack->setCurrentWidget(t);
        updateHeader();
        Q_EMIT currentPathChanged(this);
        Q_EMIT stateChanged(this);
    });
    connect(m_tabs, &QTabBar::tabCloseRequested, this, &FileDisplay::closeTab);
    connect(m_tabs, &QTabBar::tabBarClicked, this, [this](int) { Q_EMIT activated(this); });

    connect(Style::instance(), &Style::changed, this, &FileDisplay::applyStyle);
    setRole(Role::None);
}

void FileDisplay::setTreeVisible(bool visible)
{
    if (visible && !m_tree) {
        m_tree = new FolderTree(&m_aliases, m_body);
        m_tree->setShowHidden(m_showHidden);
        m_tree->setZoom(m_zoom);
        m_body->insertWidget(0, m_tree);
        m_body->setStretchFactor(0, 0);
        m_body->setStretchFactor(1, 1);
        const int w = qBound(140, width() / 4, 220);
        m_body->setSizes({w, qMax(200, width() - w)});
        connect(m_tree, &FolderTree::folderActivated, this, [this](const QString &path) {
            Q_EMIT activated(this);
            if (auto *t = currentTab())
                t->navigate(path);
        });
        if (auto *t = currentTab())
            m_tree->setPath(t->path());
    }
    if (m_tree)
        m_tree->setVisible(visible);
}

bool FileDisplay::treeVisible() const
{
    return m_tree && !m_tree->isHidden();
}

QByteArray FileDisplay::bodyState() const
{
    return m_body->saveState();
}

void FileDisplay::restoreBodyState(const QByteArray &state)
{
    if (!state.isEmpty() && m_body->count() > 1)
        m_body->restoreState(state);
}

FolderTab *FileDisplay::currentTab() const
{
    return qobject_cast<FolderTab *>(m_stack->currentWidget());
}

FolderTab *FileDisplay::tab(int index) const
{
    return qobject_cast<FolderTab *>(m_tabs->tabData(index).value<QObject *>());
}

int FileDisplay::tabCount() const
{
    return m_tabs->count();
}

int FileDisplay::currentIndex() const
{
    return m_tabs->currentIndex();
}

void FileDisplay::setCurrentIndex(int index)
{
    if (index >= 0 && index < m_tabs->count())
        m_tabs->setCurrentIndex(index);
}

FolderTab *FileDisplay::addTab(const QString &path, bool makeCurrent)
{
    auto *t = new FolderTab(m_stack);
    t->setShowHidden(m_showHidden);
    t->setZoom(m_zoom);
    m_stack->addWidget(t);
    const int insertAt = m_tabs->count() == 0 ? 0 : m_tabs->currentIndex() + 1;
    const int index = m_tabs->insertTab(insertAt, QString());
    m_tabs->setTabData(index, QVariant::fromValue<QObject *>(t));
    wireTab(t);
    t->navigate(path);
    updateTabText(t);
    if (makeCurrent || m_tabs->count() == 1) {
        m_tabs->setCurrentIndex(index);
        m_stack->setCurrentWidget(t);
    }
    updateHeader();
    return t;
}

void FileDisplay::setZoom(int percent)
{
    m_zoom = percent;
    for (int i = 0; i < tabCount(); ++i)
        tab(i)->setZoom(percent);
    if (m_tree)
        m_tree->setZoom(percent);
}

void FileDisplay::closeTab(int index)
{
    if (m_tabs->count() <= 1)
        return; // a display always keeps one tab
    FolderTab *t = tab(index);
    m_tabs->removeTab(index);
    if (t) {
        m_stack->removeWidget(t);
        t->deleteLater();
    }
    if (auto *cur = currentTab())
        m_stack->setCurrentWidget(cur);
    updateHeader();
    Q_EMIT currentPathChanged(this);
}

void FileDisplay::cycleTab(int delta)
{
    const int n = m_tabs->count();
    if (n < 2)
        return;
    m_tabs->setCurrentIndex(((m_tabs->currentIndex() + delta) % n + n) % n);
}

void FileDisplay::wireTab(FolderTab *t)
{
    connect(t, &FolderTab::pathChanged, this, [this, t]() {
        updateTabText(t);
        if (t == currentTab()) {
            updateHeader();
            Q_EMIT currentPathChanged(this);
        }
    });
    connect(t, &FolderTab::statusChanged, this, [this, t]() {
        if (t == currentTab()) {
            m_status->setText(t->statusText());
            updateFreeSpace();
        }
    });
    connect(t, &FolderTab::activated, this, [this]() { Q_EMIT activated(this); });
    for (auto sig : {&FolderTab::selectionChanged, &FolderTab::viewStateChanged, &FolderTab::statusChanged}) {
        connect(t, sig, this, [this, t]() {
            if (t == currentTab())
                Q_EMIT stateChanged(this);
        });
    }
    connect(t, &FolderTab::renameRequested, this, [this](const QString &p, const QString &n) {
        Q_EMIT renameRequested(this, p, n);
    });
    connect(t, &FolderTab::dropRequested, this, [this](const QList<QUrl> &urls, const QString &dir, Qt::DropAction a) {
        Q_EMIT dropRequested(this, urls, dir, a);
    });
    connect(t, &FolderTab::openFileRequested, this, &FileDisplay::openFileRequested);
    connect(t, &FolderTab::openInNewTabRequested, this, [this](const QString &p) { addTab(p, false); });
    connect(t, &FolderTab::contextMenuRequested, this, [this](const QPoint &pos, bool onItem) {
        Q_EMIT contextMenuRequested(this, pos, onItem);
    });
    connect(t, &FolderTab::tabKeyPressed, this, [this](bool) { Q_EMIT tabKeyPressed(this); });
    connect(t, &FolderTab::zoomRequested, this, [this](int steps) { Q_EMIT zoomRequested(this, steps); });
    connect(t, &FolderTab::commandRequested, this, [this](const QString &c) { Q_EMIT commandRequested(this, c); });
}

void FileDisplay::updateTabText(FolderTab *t)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (tab(i) == t) {
            m_tabs->setTabText(i, t->title());
            m_tabs->setTabToolTip(i, t->path());
            break;
        }
    }
}

void FileDisplay::updateHeader()
{
    FolderTab *t = currentTab();
    if (!t)
        return;
    m_crumbs->setPath(t->path());
    static_cast<FolderHandle *>(m_folderIcon)->path = t->path();
    m_recent->setEnabled(t->canGoBack() || t->canGoForward());
    m_back->setEnabled(t->canGoBack());
    m_forward->setEnabled(t->canGoForward());
    m_up->setEnabled(t->path() != QLatin1String("/"));
    if (m_tree)
        m_tree->setPath(t->path());
    m_status->setText(t->statusText());
    updateFreeSpace();
}

void FileDisplay::updateFreeSpace()
{
    FolderTab *t = currentTab();
    const QString path = t ? t->path() : QString();
    qint64 free = -1;
    qint64 total = -1;
    // Folders on this computer and its cards only: asking a server is slow.
    if (Location::isLocal(path)) {
        free = FileOps::bytesFree(path);
        total = FileOps::bytesTotal(path);
    }
    if (free < 0 || total <= 0) {
        m_free->clear();
        m_free->setToolTip(QString());
        return;
    }
    m_free->setText(QStringLiteral("%1 free of %2").arg(FileOps::formatSize(free), FileOps::formatSize(total)));
    m_free->setToolTip(QStringLiteral("%1 bytes free").arg(QLocale().toString(free)));
    // Nearly full (under 5%, or under 1 GB): shown in the warning color.
    const bool low = free < total / 20 || free < qint64(1) << 30;
    m_free->setProperty("low", low);
    QPalette pal = m_status->palette();
    if (low)
        pal.setColor(QPalette::WindowText, QColor(0xd0, 0x40, 0x30));
    m_free->setPalette(pal);
}

QString FileDisplay::freeSpaceText() const
{
    return m_free->text();
}

void FileDisplay::setRole(Role role)
{
    m_role = role;
    applyStyle();
}

void FileDisplay::applyStyle()
{
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.92);
    m_status->setFont(small);
    if (m_free)
        m_free->setFont(small);
    const Theme &theme = Style::instance()->theme();
    const QPalette pal = palette();
    auto textOn = [](const QColor &c) {
        return c.lightness() > 150 ? QStringLiteral("#101010") : QStringLiteral("white");
    };
    QString text;
    QColor accent;
    QString fg;
    switch (m_role) {
    case Role::Source:
        text = QStringLiteral("SOURCE");
        accent = theme.sourceAccent;
        break;
    case Role::Dest:
        text = m_destLocked ? QStringLiteral("DEST ●") : QStringLiteral("DEST");
        accent = m_destLocked ? theme.lockedAccent : theme.destAccent;
        break;
    case Role::None:
        text = QStringLiteral("OFF");
        break;
    }
    QPalette ap = m_accent->palette();
    ap.setColor(QPalette::Window, accent.isValid() ? accent : pal.color(QPalette::Window));
    m_accent->setPalette(ap);
    // In single mode there is nothing to tell apart: no edge.
    m_accent->setVisible(false); // the whole location bar carries the color now
    QString bg = accent.isValid() ? accent.name() : QStringLiteral("transparent");
    fg = accent.isValid() ? textOn(accent) : pal.color(QPalette::PlaceholderText).name();
    // The location bar in the role color, its icons and path in a color that reads on it.
    const QColor barColor = accent.isValid() ? accent : pal.color(QPalette::Window);
    const QColor ink = accent.isValid() ? QColor(textOn(accent)) : pal.color(QPalette::WindowText);
    QPalette hp = m_header->palette();
    hp.setColor(QPalette::Window, barColor);
    hp.setColor(QPalette::WindowText, ink);
    hp.setColor(QPalette::ButtonText, ink);
    hp.setColor(QPalette::Text, ink);
    m_header->setPalette(hp);
    m_crumbs->setPalette(hp);
    using Glyphs::Glyph;
    m_back->setIcon(Glyphs::icon(Glyph::Back, ink));
    m_forward->setIcon(Glyphs::icon(Glyph::Forward, ink));
    m_up->setIcon(Glyphs::icon(Glyph::Up, ink));
    m_refresh->setIcon(Glyphs::icon(Glyph::Refresh, ink));
    m_home->setIcon(Glyphs::icon(Glyph::Home, ink));
    m_star->setIcon(Glyphs::icon(Glyph::Star, ink));
    m_recent->setIcon(Glyphs::icon(Glyph::ChevronDown, ink));
    const int px = qMax(16, fontMetrics().height());
    m_copyPath->setIcon(Glyphs::icon(Glyph::CopyPath, ink));
    m_swapPaths->setIcon(Glyphs::icon(Glyph::Swap, ink));
    m_copyPath->setVisible(m_showBadge); // two displays showing
    m_swapPaths->setVisible(m_showBadge);
    for (QToolButton *b : {m_back, m_forward, m_up, m_refresh, m_home, m_star, m_recent, m_copyPath, m_swapPaths})
        b->setIconSize(QSize(px, px));
    m_folderIcon->setPixmap(Glyphs::pixmap(Glyph::Folder, ink, px, devicePixelRatioF()));
    m_header->setToolTip(m_role == Role::Source ? QStringLiteral("Source")
                         : m_role == Role::Dest ? (m_destLocked ? QStringLiteral("Destination (locked)") : QStringLiteral("Destination"))
                                              : QString());
    const Role role = m_role;
    m_badge->setText(text);
    m_badge->setToolTip(role == Role::Dest && m_destLocked ? QStringLiteral("Destination (locked)") : QString());
    m_badge->setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; border-radius: 4px; font-weight: 600; }").arg(bg, fg));
    // A locked destination says so in words too.
    m_badge->setVisible(m_showBadge && role == Role::Dest && m_destLocked);
}

void FileDisplay::fillRecentMenu(QMenu *menu)
{
    menu->clear();
    FolderTab *t = currentTab();
    if (!t)
        return;
    QStringList seen{t->path()};
    auto add = [&](const QString &p) {
        if (seen.contains(p))
            return;
        seen << p;
        QAction *a = menu->addAction(Location::title(p));
        a->setToolTip(Location::display(p));
        connect(a, &QAction::triggered, this, [this, p]() {
            Q_EMIT activated(this);
            if (auto *tab = currentTab())
                tab->navigate(p);
        });
    };
    const QStringList back = t->backHistory();
    for (int i = back.size() - 1; i >= 0 && seen.size() <= 20; --i)
        add(back.at(i));
    const QStringList forward = t->forwardHistory();
    if (!forward.isEmpty() && !menu->isEmpty())
        menu->addSeparator();
    for (int i = forward.size() - 1; i >= 0 && seen.size() <= 30; --i)
        add(forward.at(i));
    menu->setToolTipsVisible(true);
    if (menu->isEmpty())
        menu->addAction(QStringLiteral("(no other folders yet)"))->setEnabled(false);
}

void FileDisplay::setDestLocked(bool locked)
{
    m_destLocked = locked;
    setRole(m_role);
}

void FileDisplay::setShowRoleBadge(bool show)
{
    m_showBadge = show;
    applyStyle();
}

void FileDisplay::setShowHidden(bool show)
{
    m_showHidden = show;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *t = tab(i))
            t->setShowHidden(show);
    }
    if (m_tree)
        m_tree->setShowHidden(show);
}
