#include "ui/GroupTreeView.h"

#include "ui/FileListModel.h"
#include "ui/Style.h"

#include <QMouseEvent>
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
    connect(model, &QAbstractItemModel::modelReset, this, dirty);
    connect(model, &QAbstractItemModel::rowsInserted, this, dirty);
    connect(model, &QAbstractItemModel::rowsRemoved, this, dirty);
    connect(model, &QAbstractItemModel::layoutChanged, this, dirty);
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
    scheduleDelayedItemsLayout();
    viewport()->update();
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
    painter->drawLine(bandArea.left() + 4, bandArea.bottom() - 1, bandArea.left() + 4 + 40, bandArea.bottom() - 1);

    const QModelIndex first = index.sibling(index.row(), 0);
    const QString label = first.data(FileListModel::GroupLabelRole).toString();
    const QString key = first.data(FileListModel::GroupKeyRole).toString();
    QFont bold = font();
    bold.setBold(true);
    painter->setFont(bold);
    painter->setPen(palette().color(QPalette::Text));
    const QRect textRect = bandArea.adjusted(8, 2, -8, -2);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, label);
    const int labelWidth = QFontMetrics(bold).horizontalAdvance(label);
    painter->setFont(font());
    painter->setPen(theme.dimText);
    painter->drawText(textRect.adjusted(labelWidth + 8, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                      QStringLiteral("%1").arg(groupCount(key)));
    painter->restore();

    QStyleOptionViewItem rowOption = option;
    rowOption.rect.setTop(option.rect.top() + band);
    QTreeView::drawRow(painter, rowOption, index);
}

void GroupTreeView::mousePressEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    const QModelIndex index = indexAt(pos);
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
        event->accept(); // a double-click on a heading opens nothing
        return;
    }
    QTreeView::mouseDoubleClickEvent(event);
}
