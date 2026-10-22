#include "ui/BreadcrumbBar.h"

#include "core/AliasManager.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QStackedLayout>
#include <QStringListModel>
#include <QStyle>
#include <QTimer>
#include <QToolButton>

namespace {
constexpr int kMaxMenuEntries = 400;
}

BreadcrumbBar::BreadcrumbBar(AliasManager &aliases, QWidget *parent)
    : QWidget(parent)
    , m_aliases(aliases)
    , m_stack(new QStackedLayout(this))
    , m_crumbArea(new QWidget(this))
    , m_crumbLayout(new QHBoxLayout(m_crumbArea))
    , m_overflow(new QToolButton(m_crumbArea))
    , m_edit(new QLineEdit(this))
    , m_completer(new QCompleter(this))
    , m_completions(new QStringListModel(this))
{
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->addWidget(m_crumbArea);
    m_stack->addWidget(m_edit);

    m_crumbLayout->setContentsMargins(0, 0, 0, 0);
    m_crumbLayout->setSpacing(0);
    m_overflow->setText(QStringLiteral("«"));
    m_overflow->setToolTip(QStringLiteral("More of this path"));
    m_overflow->setAutoRaise(true);
    m_overflow->setPopupMode(QToolButton::InstantPopup);
    m_overflow->setMenu(new QMenu(m_overflow));
    m_overflow->hide();
    m_crumbLayout->addWidget(m_overflow);
    m_crumbLayout->addStretch(1);

    m_completer->setModel(m_completions);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_edit->setCompleter(m_completer);
    m_edit->setClearButtonEnabled(true);
    m_edit->setPlaceholderText(QStringLiteral("Path or /alias"));
    m_edit->installEventFilter(this);

    connect(m_edit, &QLineEdit::textEdited, this, &BreadcrumbBar::updateCompletions);
    connect(m_edit, &QLineEdit::returnPressed, this, [this]() {
        if (m_completer->popup() && m_completer->popup()->isVisible())
            return;
        const QString text = m_edit->text().trimmed();
        stopEditing();
        if (!text.isEmpty())
            Q_EMIT navigateRequested(text);
    });
    connect(m_completer, QOverload<const QString &>::of(&QCompleter::activated), this, [this](const QString &text) {
        m_edit->setText(text);
        // A completed folder keeps going: offer its children next.
        QTimer::singleShot(0, this, [this, text]() { updateCompletions(text); });
    });
}

QSize BreadcrumbBar::sizeHint() const
{
    return QSize(300, m_edit->sizeHint().height());
}

QSize BreadcrumbBar::minimumSizeHint() const
{
    return QSize(60, m_edit->sizeHint().height());
}

void BreadcrumbBar::setPath(const QString &path)
{
    if (path == m_path && !m_crumbs.isEmpty())
        return;
    m_path = path;
    rebuild();
}

bool BreadcrumbBar::isEditing() const
{
    return m_stack->currentWidget() == m_edit;
}

void BreadcrumbBar::startEditing()
{
    m_edit->setText(m_path);
    m_stack->setCurrentWidget(m_edit);
    m_edit->setFocus(Qt::ShortcutFocusReason);
    m_edit->selectAll();
}

void BreadcrumbBar::stopEditing()
{
    if (m_completer->popup())
        m_completer->popup()->hide();
    m_stack->setCurrentWidget(m_crumbArea);
}

void BreadcrumbBar::rebuild()
{
    for (Crumb &c : m_crumbs) {
        delete c.button;
        delete c.arrow;
    }
    m_crumbs.clear();

    // Paths under the home folder start at "Home".
    const QString home = QDir::homePath();
    QString base;
    QStringList parts;
    if (m_path == home || m_path.startsWith(home + QLatin1Char('/'))) {
        m_crumbs.append({QStringLiteral("Home"), home, nullptr, nullptr});
        base = home;
        parts = m_path.mid(home.size()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    } else {
        m_crumbs.append({QStringLiteral("/"), QStringLiteral("/"), nullptr, nullptr});
        base = QString();
        parts = m_path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    }
    for (const QString &part : std::as_const(parts)) {
        base += QLatin1Char('/') + part;
        m_crumbs.append({part, base, nullptr, nullptr});
    }

    int insertAt = 1; // after the overflow button
    for (int i = 0; i < m_crumbs.size(); ++i) {
        Crumb &c = m_crumbs[i];
        c.button = new QToolButton(m_crumbArea);
        c.button->setAutoRaise(true);
        c.button->setText(c.label);
        c.button->setToolTip(c.path);
        c.button->setFocusPolicy(Qt::NoFocus);
        if (i == 0 && c.label == QLatin1String("Home"))
            c.button->setIcon(QIcon::fromTheme(QStringLiteral("user-home")));
        if (i == m_crumbs.size() - 1) {
            QFont f = c.button->font();
            f.setBold(true);
            c.button->setFont(f);
        }
        if (!c.button->icon().isNull())
            c.button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        const QString target = c.path;
        connect(c.button, &QToolButton::clicked, this, [this, target]() { Q_EMIT navigateRequested(target); });
        m_crumbLayout->insertWidget(insertAt++, c.button);

        c.arrow = new QToolButton(m_crumbArea);
        c.arrow->setAutoRaise(true);
        c.arrow->setArrowType(Qt::RightArrow);
        c.arrow->setFocusPolicy(Qt::NoFocus);
        c.arrow->setToolTip(QStringLiteral("Folders in %1").arg(c.label));
        c.arrow->setPopupMode(QToolButton::InstantPopup);
        c.arrow->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));
        c.arrow->setFixedWidth(16);
        auto *menu = new QMenu(c.arrow);
        c.arrow->setMenu(menu);
        QToolButton *arrow = c.arrow;
        connect(menu, &QMenu::aboutToShow, this, [this, arrow, target]() { fillChildMenu(arrow, target); });
        m_crumbLayout->insertWidget(insertAt++, c.arrow);
    }
    relayout();
}

void BreadcrumbBar::fillChildMenu(QToolButton *arrow, const QString &dir)
{
    QMenu *menu = arrow->menu();
    menu->clear();
    const QStringList children = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    int count = 0;
    const QIcon folder = QIcon::fromTheme(QStringLiteral("folder"));
    for (const QString &child : children) {
        if (++count > kMaxMenuEntries) {
            menu->addAction(QStringLiteral("(%1 more)").arg(children.size() - kMaxMenuEntries))->setEnabled(false);
            break;
        }
        const QString full = QDir(dir).filePath(child);
        QAction *a = menu->addAction(folder, child);
        if (m_path.startsWith(full + QLatin1Char('/')) || m_path == full) {
            QFont f = a->font();
            f.setBold(true);
            a->setFont(f);
        }
        connect(a, &QAction::triggered, this, [this, full]() { Q_EMIT navigateRequested(full); });
    }
    if (children.isEmpty())
        menu->addAction(QStringLiteral("(no folders)"))->setEnabled(false);
}

void BreadcrumbBar::relayout()
{
    if (m_crumbs.isEmpty())
        return;
    // Show as many trailing segments as fit; earlier ones move into «.
    const int available = m_crumbArea->width() > 0 ? m_crumbArea->width() : width();
    int used = 0;
    int firstVisible = m_crumbs.size() - 1;
    const int overflowWidth = m_overflow->sizeHint().width();
    for (int i = m_crumbs.size() - 1; i >= 0; --i) {
        const int w = m_crumbs[i].button->sizeHint().width() + m_crumbs[i].arrow->width();
        const int reserve = i > 0 ? overflowWidth : 0;
        if (used + w + reserve > available && i != m_crumbs.size() - 1)
            break;
        used += w;
        firstVisible = i;
    }

    QMenu *menu = m_overflow->menu();
    menu->clear();
    for (int i = 0; i < m_crumbs.size(); ++i) {
        const bool visible = i >= firstVisible;
        m_crumbs[i].button->setVisible(visible);
        m_crumbs[i].arrow->setVisible(visible);
        if (!visible) {
            const QString target = m_crumbs[i].path;
            QAction *a = menu->addAction(m_crumbs[i].label);
            connect(a, &QAction::triggered, this, [this, target]() { Q_EMIT navigateRequested(target); });
        }
    }
    m_overflow->setVisible(firstVisible > 0);
}

void BreadcrumbBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void BreadcrumbBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !isEditing()) {
        startEditing();
        return;
    }
    QWidget::mousePressEvent(event);
}

bool BreadcrumbBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit) {
        if (event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Escape) {
                stopEditing();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            // Leaving the field (and not for its own completion popup) cancels.
            QTimer::singleShot(0, this, [this]() {
                if (isEditing() && !m_edit->hasFocus() && !(m_completer->popup() && m_completer->popup()->isVisible()))
                    stopEditing();
            });
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BreadcrumbBar::updateCompletions(const QString &text)
{
    QStringList out;
    if (text.startsWith(QLatin1Char('/')) && text.indexOf(QLatin1Char('/'), 1) == -1) {
        // "/do" -> aliases, then real top-level folders.
        out << m_aliases.completions(text);
        const QString prefix = text.mid(1);
        const QStringList roots = QDir(QStringLiteral("/")).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &r : roots) {
            if (r.startsWith(prefix, Qt::CaseInsensitive))
                out << QLatin1Char('/') + r + QLatin1Char('/');
        }
    } else {
        const int slash = text.lastIndexOf(QLatin1Char('/'));
        if (slash >= 0) {
            const QString typedDir = text.left(slash + 1);
            const QString fragment = text.mid(slash + 1);
            const QString realDir = m_aliases.resolve(typedDir.isEmpty() ? QStringLiteral("/") : typedDir);
            if (QDir::isAbsolutePath(realDir)) {
                const QStringList children = QDir(realDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                                                                     QDir::Name | QDir::IgnoreCase);
                for (const QString &c : children) {
                    if (c.startsWith(QLatin1Char('.')) && !fragment.startsWith(QLatin1Char('.')))
                        continue;
                    if (c.startsWith(fragment, Qt::CaseInsensitive))
                        out << typedDir + c + QLatin1Char('/');
                    if (out.size() >= 200)
                        break;
                }
            }
        }
    }
    m_completions->setStringList(out);
    if (!out.isEmpty())
        m_completer->complete();
    else if (m_completer->popup())
        m_completer->popup()->hide();
}
