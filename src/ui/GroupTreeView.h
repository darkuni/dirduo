#pragma once

#include <QHash>
#include <QTreeView>

// The details view. When grouping is on, the first row of each group carries
// a heading band above it ("Images (12)"); clicking the band selects the
// whole group, Ctrl+click adds it to the selection. Rows stay a flat list, so
// selection, rename and drag and drop work exactly as without groups.
class GroupTreeView : public QTreeView
{
    Q_OBJECT
public:
    explicit GroupTreeView(QWidget *parent = nullptr);

    void setGrouping(bool on);
    bool grouping() const { return m_grouping; }

    int bandHeight() const;
    // First visible row of its group.
    bool isGroupLeader(const QModelIndex &index) const;
    // The heading strip of a leader row, in viewport coordinates.
    QRect bandRect(const QModelIndex &index) const;
    // Rows that share index's group (proxy rows).
    QList<int> groupRows(const QModelIndex &index) const;

    void setModel(QAbstractItemModel *model) override;

protected:
    void drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    int groupCount(const QString &key) const;

    bool m_grouping = false;
    mutable QHash<QString, int> m_counts;
    mutable bool m_countsDirty = true;
};
