#include "ui/ToolbarStrip.h"

#include <QBoxLayout>
#include <QEvent>
#include <QToolBar>

ToolbarStrip::ToolbarStrip(QWidget *parent)
    : QWidget(parent)
    , m_layout(new QBoxLayout(QBoxLayout::LeftToRight, this))
{
    setObjectName(QStringLiteral("toolbar-strip"));
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    setAutoFillBackground(true);
    hide();
}

void ToolbarStrip::setSplitOrientation(Qt::Orientation splitOrientation)
{
    m_split = splitOrientation;
    relayout();
}

void ToolbarStrip::addBar(QToolBar *bar, int index)
{
    if (m_bars.contains(bar))
        return;
    if (index < 0 || index > m_bars.size())
        index = m_bars.size();
    m_bars.insert(index, bar);
    bar->setParent(this);
    bar->setMovable(false); // its place is chosen from its menu
    relayout();
}

void ToolbarStrip::removeBar(QToolBar *bar)
{
    if (!m_bars.removeOne(bar))
        return;
    m_layout->removeWidget(bar);
    relayout();
}

void ToolbarStrip::moveBar(QToolBar *bar, int delta)
{
    const int from = m_bars.indexOf(bar);
    if (from < 0)
        return;
    const int to = qBound(0, from + delta, static_cast<int>(m_bars.size()) - 1);
    if (to == from)
        return;
    m_bars.move(from, to);
    relayout();
}

void ToolbarStrip::relayout()
{
    while (m_layout->count() > 0)
        delete m_layout->takeAt(0);

    // Side-by-side displays: vertical bars, side by side in columns.
    // Stacked displays: horizontal bars, stacked in rows.
    const bool sideBySide = m_split == Qt::Horizontal;
    m_layout->setDirection(sideBySide ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    for (QToolBar *bar : std::as_const(m_bars)) {
        bar->setOrientation(sideBySide ? Qt::Vertical : Qt::Horizontal);
        m_layout->addWidget(bar, 0, sideBySide ? Qt::AlignTop : Qt::AlignLeft);
    }
    m_layout->addStretch(1);
    updateGeometryForBars();
}

void ToolbarStrip::updateGeometryForBars()
{
    bool anyVisible = false;
    for (QToolBar *bar : std::as_const(m_bars))
        anyVisible = anyVisible || !bar->isHidden();
    setVisible(anyVisible);
    if (!anyVisible)
        return;

    // Only as thick as the bars; as long as the displays.
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
