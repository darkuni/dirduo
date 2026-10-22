#include "ui/FileDisplay.h"

#include "ui/BreadcrumbBar.h"
#include "ui/FolderTree.h"
#include "ui/Style.h"

#include <QSplitter>

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
    m_badge->setAlignment(Qt::AlignCenter);
    m_badge->setMinimumWidth(64);
    m_badge->setContentsMargins(6, 1, 6, 1);
    hl->addWidget(m_badge);
    hl->addWidget(m_back);
    hl->addWidget(m_forward);
    hl->addWidget(m_up);
    hl->addWidget(m_crumbs, 1);
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
    layout->addWidget(m_status);

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
        if (t == currentTab())
            m_status->setText(t->statusText());
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
    m_back->setEnabled(t->canGoBack());
    m_forward->setEnabled(t->canGoForward());
    m_up->setEnabled(t->path() != QLatin1String("/"));
    if (m_tree)
        m_tree->setPath(t->path());
    m_status->setText(t->statusText());
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
    m_accent->setVisible(m_showBadge && accent.isValid());
    QString bg = accent.isValid() ? accent.name() : QStringLiteral("transparent");
    fg = accent.isValid() ? textOn(accent) : pal.color(QPalette::PlaceholderText).name();
    const Role role = m_role;
    m_badge->setText(text);
    m_badge->setToolTip(role == Role::Dest && m_destLocked ? QStringLiteral("Destination (locked)") : QString());
    m_badge->setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; border-radius: 4px; font-weight: 600; }").arg(bg, fg));
    m_badge->setVisible(m_showBadge);
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
