#include "ui/ToolbarStrip.h"

#include "ui/Style.h"

#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QToolBar>

ToolbarStrip::ToolbarStrip(QWidget *parent)
    : QWidget(parent)
    , m_layout(new QBoxLayout(QBoxLayout::LeftToRight, this))
    , m_indicator(new QWidget(this))
{
    setObjectName(QStringLiteral("toolbar-strip"));
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    setAutoFillBackground(true);
    m_indicator->setAutoFillBackground(true);
    QPalette pal = m_indicator->palette();
    pal.setColor(QPalette::Window, palette().color(QPalette::Highlight));
    m_indicator->setPalette(pal);
    m_indicator->hide();
    hide();
    // A bigger text size makes the bars wider: measure again once they have
    // taken the new font, or the drive names get cut short.
    connect(Style::instance(), &Style::changed, this, [this]() {
        QTimer::singleShot(0, this, [this]() {
            if (!m_lines.isEmpty())
                updateGeometryForBars();
        });
    });
}

void ToolbarStrip::setSplitOrientation(Qt::Orientation splitOrientation)
{
    m_split = splitOrientation;
    relayout();
}

bool ToolbarStrip::contains(QToolBar *bar) const
{
    for (const auto &line : m_lines) {
        if (line.contains(bar))
            return true;
    }
    return false;
}

QList<QToolBar *> ToolbarStrip::bars() const
{
    QList<QToolBar *> out;
    for (const auto &line : m_lines)
        out += line;
    return out;
}

bool ToolbarStrip::isJoined(QToolBar *bar) const
{
    for (const auto &line : m_lines) {
        const int at = line.indexOf(bar);
        if (at >= 0)
            return at > 0;
    }
    return false;
}

void ToolbarStrip::addBar(QToolBar *bar, int line, bool joinLast)
{
    if (contains(bar))
        return;
    if (joinLast && !m_lines.isEmpty()) {
        m_lines.last().append(bar);
    } else {
        if (line < 0 || line > m_lines.size())
            line = int(m_lines.size());
        m_lines.insert(line, QList<QToolBar *>{bar});
    }
    bar->setParent(this);
    bar->setMovable(false); // moved by dragging an empty spot, or from its menu
    bar->installEventFilter(this);
    relayout();
}

void ToolbarStrip::removeBar(QToolBar *bar)
{
    bool found = false;
    for (auto &line : m_lines)
        found = line.removeOne(bar) || found;
    if (!found)
        return;
    bar->removeEventFilter(this);
    m_layout->removeWidget(bar);
    prune();
    relayout();
}

void ToolbarStrip::prune()
{
    m_lines.removeIf([](const QList<QToolBar *> &l) { return l.isEmpty(); });
}

void ToolbarStrip::setJoined(QToolBar *bar, bool joined)
{
    for (int i = 0; i < m_lines.size(); ++i) {
        const int at = m_lines[i].indexOf(bar);
        if (at < 0)
            continue;
        if (joined && at == 0 && i > 0) {
            m_lines[i].removeAt(0);
            m_lines[i - 1].append(bar);
        } else if (!joined && at > 0) {
            // It and what follows it on the line start a line of their own.
            const QList<QToolBar *> rest = m_lines[i].mid(at);
            m_lines[i] = m_lines[i].mid(0, at);
            m_lines.insert(i + 1, rest);
        } else {
            return;
        }
        prune();
        relayout();
        Q_EMIT arrangementChanged();
        return;
    }
}

bool ToolbarStrip::canMove(QToolBar *bar, int delta) const
{
    for (int i = 0; i < m_lines.size(); ++i) {
        const int at = m_lines[i].indexOf(bar);
        if (at < 0)
            continue;
        if (m_lines[i].size() > 1)
            return at + delta >= 0 && at + delta < m_lines[i].size();
        return i + delta >= 0 && i + delta < m_lines.size();
    }
    return false;
}

void ToolbarStrip::moveBar(QToolBar *bar, int delta)
{
    if (!canMove(bar, delta))
        return;
    for (int i = 0; i < m_lines.size(); ++i) {
        const int at = m_lines[i].indexOf(bar);
        if (at < 0)
            continue;
        if (m_lines[i].size() > 1)
            m_lines[i].move(at, at + delta);
        else
            m_lines.move(i, i + delta);
        break;
    }
    relayout();
    Q_EMIT arrangementChanged();
}

void ToolbarStrip::placeBar(QToolBar *bar, int line, int position, bool newLine)
{
    // Where it was, so the indexes can allow for taking it out.
    int fromLine = -1, fromAt = -1;
    for (int i = 0; i < m_lines.size(); ++i) {
        const int at = m_lines[i].indexOf(bar);
        if (at >= 0) {
            fromLine = i;
            fromAt = at;
        }
    }
    if (fromLine < 0)
        return;
    const bool alone = m_lines[fromLine].size() == 1;
    m_lines[fromLine].removeAt(fromAt);
    if (newLine) {
        if (alone && fromLine < line)
            --line;
        if (alone)
            m_lines.removeAt(fromLine);
        line = qBound(0, line, int(m_lines.size()));
        m_lines.insert(line, QList<QToolBar *>{bar});
    } else {
        if (fromLine == line && fromAt < position)
            --position;
        if (alone && fromLine != line) {
            m_lines.removeAt(fromLine);
            if (fromLine < line)
                --line;
        }
        line = qBound(0, line, int(m_lines.size()) - 1);
        if (line < 0) {
            m_lines.append(QList<QToolBar *>{bar});
        } else {
            position = qBound(0, position, int(m_lines[line].size()));
            m_lines[line].insert(position, bar);
        }
    }
    prune();
    relayout();
    Q_EMIT arrangementChanged();
}

void ToolbarStrip::relayout()
{
    while (m_layout->count() > 0) {
        QLayoutItem *item = m_layout->takeAt(0);
        if (QLayout *inner = item->layout()) {
            while (inner->count() > 0)
                delete inner->takeAt(0);
        }
        delete item;
    }

    // Side-by-side displays: vertical bars, lines are columns side by side.
    // Stacked displays: horizontal bars, lines are rows stacked.
    const bool side = sideBySide();
    m_layout->setDirection(side ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    for (const auto &line : std::as_const(m_lines)) {
        auto *box = new QBoxLayout(side ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        box->setSpacing(0);
        bool filled = false;
        for (QToolBar *bar : line) {
            bar->setOrientation(side ? Qt::Vertical : Qt::Horizontal);
            // A bar with a spacer in it takes the rest of the line, so what
            // follows the spacer sits at the far end.
            const bool fill = bar->property("linopusFill").toBool();
            filled = filled || fill;
            box->addWidget(bar, fill ? 1 : 0);
        }
        if (!filled)
            box->addStretch(1);
        m_layout->addLayout(box);
    }
    m_layout->addStretch(1);
    updateGeometryForBars();
}

void ToolbarStrip::updateGeometryForBars()
{
    bool anyVisible = false;
    for (QToolBar *bar : bars())
        anyVisible = anyVisible || !bar->isHidden();
    setVisible(anyVisible);
    if (!anyVisible)
        return;

    // Only as thick as the bars; as long as the displays.
    m_layout->invalidate();
    m_layout->activate();
    const QSize hint = m_layout->sizeHint();
    if (m_split == Qt::Horizontal) {
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setFixedWidth(hint.width());
    } else {
        setMinimumWidth(0);
        setMaximumWidth(QWIDGETSIZE_MAX);
        setFixedHeight(hint.height());
    }
}

QRect ToolbarStrip::lineRect(int line) const
{
    QRect r;
    for (QToolBar *bar : m_lines.value(line)) {
        if (!bar->isHidden())
            r = r.united(bar->geometry());
    }
    if (r.isNull())
        return r;
    // The whole length of the strip: dropping past the last bar joins the line.
    if (sideBySide()) {
        r.setTop(0);
        r.setBottom(height() - 1);
    } else {
        r.setLeft(0);
        r.setRight(width() - 1);
    }
    return r;
}

ToolbarStrip::Drop ToolbarStrip::dropAt(const QPoint &pos, QToolBar *dragged) const
{
    Drop d;
    const bool side = sideBySide();
    auto cross = [side](const QPoint &p) { return side ? p.x() : p.y(); };
    auto along = [side](const QPoint &p) { return side ? p.y() : p.x(); };
    for (int i = 0; i < m_lines.size(); ++i) {
        const QRect r = lineRect(i);
        if (r.isNull())
            continue;
        const int lo = side ? r.left() : r.top();
        const int hi = side ? r.right() : r.bottom();
        const int c = cross(pos);
        if (c < lo || c > hi)
            continue;
        const int edge = qMax(4, (hi - lo) / 4);
        d.valid = true;
        if (c < lo + edge) {
            d.line = i;
            d.newLine = true;
        } else if (c > hi - edge) {
            d.line = i + 1;
            d.newLine = true;
        } else {
            d.line = i;
            d.position = 0;
            for (QToolBar *bar : m_lines[i]) {
                if (bar->isHidden())
                    continue;
                const QRect g = bar->geometry();
                const int mid = side ? g.center().y() : g.center().x();
                if (along(pos) > mid)
                    d.position = int(m_lines[i].indexOf(bar)) + 1;
            }
        }
        // Dropping it where it is does nothing.
        const int at = m_lines[i].indexOf(dragged);
        if (!d.newLine && at >= 0 && (d.position == at || d.position == at + 1))
            d.valid = false;
        if (d.newLine && m_lines[i].size() == 1 && m_lines[i].first() == dragged)
            d.valid = false;
        return d;
    }
    return d;
}

void ToolbarStrip::showIndicator(const Drop &d)
{
    if (!d.valid) {
        m_indicator->hide();
        return;
    }
    const bool side = sideBySide();
    const int t = 3;
    QRect r;
    if (d.newLine) {
        int at;
        if (d.line < m_lines.size()) {
            const QRect lr = lineRect(d.line);
            at = side ? lr.left() : lr.top();
        } else {
            const QRect lr = lineRect(int(m_lines.size()) - 1);
            at = side ? lr.right() + 1 : lr.bottom() + 1;
        }
        r = side ? QRect(at - t / 2, 0, t, height()) : QRect(0, at - t / 2, width(), t);
    } else {
        const QRect lr = lineRect(d.line);
        const auto &line = m_lines[d.line];
        int at;
        if (d.position < line.size())
            at = side ? line[d.position]->geometry().top() : line[d.position]->geometry().left();
        else
            at = side ? line.last()->geometry().bottom() + 1 : line.last()->geometry().right() + 1;
        r = side ? QRect(lr.left(), at - t / 2, lr.width(), t) : QRect(at - t / 2, lr.top(), t, lr.height());
    }
    m_indicator->setGeometry(r);
    m_indicator->raise();
    m_indicator->show();
}

bool ToolbarStrip::eventFilter(QObject *watched, QEvent *event)
{
    auto *bar = qobject_cast<QToolBar *>(watched);
    if (!bar || !contains(bar))
        return QWidget::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            m_pressBar = bar;
            m_pressPos = me->globalPosition().toPoint();
            m_dragging = false;
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (m_pressBar != bar || !(me->buttons() & Qt::LeftButton))
            break;
        const QPoint global = me->globalPosition().toPoint();
        if (!m_dragging && (global - m_pressPos).manhattanLength() < QApplication::startDragDistance() * 2)
            break;
        if (!m_dragging) {
            m_dragging = true;
            bar->setCursor(Qt::ClosedHandCursor);
        }
        showIndicator(dropAt(mapFromGlobal(global), bar));
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (m_pressBar != bar)
            break;
        m_pressBar = nullptr;
        if (!m_dragging)
            break;
        m_dragging = false;
        bar->unsetCursor();
        m_indicator->hide();
        const Drop d = dropAt(mapFromGlobal(me->globalPosition().toPoint()), bar);
        if (d.valid)
            placeBar(bar, d.line, d.position, d.newLine);
        return true;
    }
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}
