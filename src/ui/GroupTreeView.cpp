#include "ui/GroupTreeView.h"

#include "ui/FileListModel.h"
#include "ui/Style.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QPainter>

GroupTreeView::GroupTreeView(QWidget *parent)
    : QTreeView(parent)
{
    connect(Style::instance(), &Style::changed, viewport(), qOverload<>(&QWidget::update));
}

void GroupTreeView::setModel(QAbstractItemModel *model)
{
    QTreeView::setModel(model);
    auto dirty = [this]() { m_countsDirty = true; };
    auto reshaped = [this]() {
        m_countsDirty = true;
        scheduleApply(); // rows moved: hide the right ones again
    };
    connect(model, &QAbstractItemModel::modelReset, this, reshaped);
    connect(model, &QAbstractItemModel::rowsInserted, this, reshaped);
    connect(model, &QAbstractItemModel::rowsRemoved, this, reshaped);
    connect(model, &QAbstractItemModel::layoutChanged, this, reshaped);
    connect(model, &QAbstractItemModel::dataChanged, this, dirty);
}

void GroupTreeView::setGrouping(bool on)
{
    if (m_grouping == on)
        return;
    m_grouping = on;
    m_countsDirty = true;
    // Leader rows are taller; rows can no longer share one height.
    setUniformRowHeights(!on);
    applyCollapsed();
    scheduleDelayedItemsLayout();
    viewport()->update();
}

QString GroupTreeView::keyOf(const QModelIndex &index) const
{
    return index.sibling(index.row(), 0).data(FileListModel::GroupKeyRole).toString();
}

bool GroupTreeView::isCollapsed(const QModelIndex &index) const
{
    return m_grouping && index.isValid() && m_collapsed.contains(keyOf(index));
}

void GroupTreeView::setGroupCollapsed(const QString &key, bool collapsed)
{
    if (collapsed == m_collapsed.contains(key))
        return;
    if (collapsed)
        m_collapsed.insert(key);
    else
        m_collapsed.remove(key);
    applyCollapsed();
    Q_EMIT collapsedChanged();
}

void GroupTreeView::toggleGroup(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    const QString key = keyOf(index);
    const bool collapse = !m_collapsed.contains(key);
    if (collapse) {
        // Keep the cursor on the heading, the one row that stays visible.
        const QList<int> rows = groupRows(index);
        if (!rows.isEmpty() && selectionModel())
            selectionModel()->setCurrentIndex(model()->index(rows.first(), 0), QItemSelectionModel::NoUpdate);
    }
    setGroupCollapsed(key, collapse);
}

QStringList GroupTreeView::groupKeys() const
{
    QStringList keys;
    const int rows = model() ? model()->rowCount() : 0;
    for (int r = 0; r < rows; ++r) {
        const QString k = model()->index(r, 0).data(FileListModel::GroupKeyRole).toString();
        if (keys.isEmpty() || keys.last() != k)
            keys << k;
    }
    return keys;
}

void GroupTreeView::collapseAll()
{
    for (const QString &k : groupKeys())
        m_collapsed.insert(k);
    applyCollapsed();
    Q_EMIT collapsedChanged();
}

void GroupTreeView::expandAll()
{
    m_collapsed.clear();
    applyCollapsed();
    Q_EMIT collapsedChanged();
}

void GroupTreeView::scheduleApply()
{
    if (m_applyPending)
        return;
    m_applyPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_applyPending = false;
        applyCollapsed();
    });
}

void GroupTreeView::applyCollapsed()
{
    if (!model())
        return;
    const int rows = model()->rowCount();
    QString prev;
    for (int r = 0; r < rows; ++r) {
        const QString key = model()->index(r, 0).data(FileListModel::GroupKeyRole).toString();
        const bool leader = r == 0 || key != prev;
        prev = key;
        const bool hide = m_grouping && !leader && m_collapsed.contains(key);
        if (isRowHidden(r, QModelIndex()) != hide)
            setRowHidden(r, QModelIndex(), hide);
    }
    doItemsLayout(); // leader rows change height when their group folds
    viewport()->update();
}

QRect GroupTreeView::arrowRect(const QModelIndex &index) const
{
    const QRect band = bandRect(index);
    return QRect(band.left() + 4, band.top() + 2, 18, band.height() - 4);
}

int GroupTreeView::bandHeight() const
{
    return fontMetrics().height() + 12;
}

bool GroupTreeView::isGroupLeader(const QModelIndex &index) const
{
    if (!m_grouping || !index.isValid())
        return false;
    if (index.row() == 0)
        return true;
    const QModelIndex prev = model()->index(index.row() - 1, 0);
    return prev.data(FileListModel::GroupKeyRole).toString()
        != index.sibling(index.row(), 0).data(FileListModel::GroupKeyRole).toString();
}

QRect GroupTreeView::bandRect(const QModelIndex &index) const
{
    const QRect row = visualRect(index.sibling(index.row(), 0));
    return QRect(0, row.top(), viewport()->width(), bandHeight());
}

int GroupTreeView::groupCount(const QString &key) const
{
    if (m_countsDirty) {
        m_counts.clear();
        const int rows = model() ? model()->rowCount() : 0;
        for (int r = 0; r < rows; ++r)
            ++m_counts[model()->index(r, 0).data(FileListModel::GroupKeyRole).toString()];
        m_countsDirty = false;
    }
    return m_counts.value(key);
}

QList<int> GroupTreeView::groupRows(const QModelIndex &index) const
{
    QList<int> rows;
    if (!index.isValid())
        return rows;
    const QString key = index.sibling(index.row(), 0).data(FileListModel::GroupKeyRole).toString();
    // Groups are contiguous: walk out from the row in both directions.
    int r = index.row();
    while (r > 0 && model()->index(r - 1, 0).data(FileListModel::GroupKeyRole).toString() == key)
        --r;
    for (; r < model()->rowCount(); ++r) {
        if (model()->index(r, 0).data(FileListModel::GroupKeyRole).toString() != key)
            break;
        rows << r;
    }
    return rows;
}

void GroupTreeView::drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (!isGroupLeader(index)) {
        QTreeView::drawRow(painter, option, index);
        return;
    }
    const Theme &theme = Style::instance()->theme();
    const int band = bandHeight();
    const QRect bandArea(0, option.rect.top(), viewport()->width(), band);

    painter->save();
    painter->fillRect(bandArea.adjusted(0, 2, 0, -1), theme.groupHeader);
    QPen accent(theme.sourceAccent);
    accent.setWidth(2);
    painter->setPen(accent);
    painter->drawLine(bandArea.left() + 26, bandArea.bottom() - 1, bandArea.left() + 26 + 40, bandArea.bottom() - 1);

    const QModelIndex first = index.sibling(index.row(), 0);
    const QString label = first.data(FileListModel::GroupLabelRole).toString();
    const QString key = first.data(FileListModel::GroupKeyRole).toString();
    const bool collapsed = m_collapsed.contains(key);

    // Arrow: pointing right when folded, down when open.
    const QRectF arrow = arrowRect(index);
    const QPointF c = arrow.center();
    const qreal a = qMin(arrow.width(), arrow.height()) * 0.22;
    QPolygonF tri;
    if (collapsed)
        tri << QPointF(c.x() - a * 0.6, c.y() - a) << QPointF(c.x() + a * 0.8, c.y()) << QPointF(c.x() - a * 0.6, c.y() + a);
    else
        tri << QPointF(c.x() - a, c.y() - a * 0.6) << QPointF(c.x() + a, c.y() - a * 0.6) << QPointF(c.x(), c.y() + a * 0.8);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(palette().color(QPalette::Text));
    painter->drawPolygon(tri);
    QFont bold = font();
    bold.setBold(true);
    painter->setFont(bold);
    painter->setPen(palette().color(QPalette::Text));
    const QRect textRect = bandArea.adjusted(26, 2, -8, -2);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, label);
    const int labelWidth = QFontMetrics(bold).horizontalAdvance(label);
    painter->setFont(font());
    painter->setPen(theme.dimText);
    painter->drawText(textRect.adjusted(labelWidth + 8, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                      QStringLiteral("%1").arg(groupCount(key)));
    painter->restore();

    if (collapsed)
        return; // a folded group shows its heading only
    QStyleOptionViewItem rowOption = option;
    rowOption.rect.setTop(option.rect.top() + band);
    QTreeView::drawRow(painter, rowOption, index);
}

void GroupTreeView::mousePressEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    const QModelIndex index = indexAt(pos);
    if (index.isValid() && isGroupLeader(index) && arrowRect(index).contains(pos) && event->button() == Qt::LeftButton) {
        toggleGroup(index);
        event->accept();
        return;
    }
    if (index.isValid() && isGroupLeader(index) && bandRect(index).contains(pos) && event->button() == Qt::LeftButton) {
        QItemSelection sel;
        const QList<int> rows = groupRows(index);
        if (!rows.isEmpty())
            sel.select(model()->index(rows.first(), 0), model()->index(rows.last(), model()->columnCount() - 1));
        const bool add = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
        selectionModel()->select(sel, (add ? QItemSelectionModel::Select : QItemSelectionModel::ClearAndSelect)
                                          | QItemSelectionModel::Rows);
        selectionModel()->setCurrentIndex(index.sibling(index.row(), 0), QItemSelectionModel::NoUpdate);
        event->accept();
        return;
    }
    QTreeView::mousePressEvent(event);
}

void GroupTreeView::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    const QModelIndex index = indexAt(pos);
    if (index.isValid() && isGroupLeader(index) && bandRect(index).contains(pos)) {
        if (!arrowRect(index).contains(pos))
            toggleGroup(index); // the arrow already toggled on the press
        event->accept();
        return;
    }
    QTreeView::mouseDoubleClickEvent(event);
}

void GroupTreeView::keyPressEvent(QKeyEvent *event)
{
    // Left folds the group the cursor is in, Right opens it.
    if (m_grouping && event->modifiers() == Qt::NoModifier
        && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
        const QModelIndex cur = currentIndex();
        if (cur.isValid()) {
            const bool collapse = event->key() == Qt::Key_Left;
            if (collapse != m_collapsed.contains(keyOf(cur)))
                toggleGroup(cur);
            event->accept();
            return;
        }
    }
    QTreeView::keyPressEvent(event);
}
