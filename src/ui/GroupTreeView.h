#pragma once

#include <QHash>
#include <QSet>
#include <QTreeView>

// The details view. When grouping is on, the first row of each group carries
// a heading band above it ("Images (12)"); clicking the band selects the
// whole group, Ctrl+click adds it to the selection, and the arrow (or a
// double-click, or Left/Right on the keyboard) collapses and expands it. Rows
// stay a flat list, so selection, rename and drag and drop work exactly as
// without groups; a collapsed group keeps its first row as a heading only.
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

    // Collapsed groups are remembered by key, so they stay collapsed across
    // refreshes and in the next folder with the same grouping.
    bool isCollapsed(const QModelIndex &index) const;
    bool isGroupCollapsed(const QString &key) const { return m_collapsed.contains(key); }
    void setGroupCollapsed(const QString &key, bool collapsed);
    void toggleGroup(const QModelIndex &index);
    void collapseAll();
    void expandAll();
    QStringList groupKeys() const;
    // The arrow at the left of a heading band, in viewport coordinates.
    QRect arrowRect(const QModelIndex &index) const;

    void setModel(QAbstractItemModel *model) override;

protected:
    void drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

Q_SIGNALS:
    void collapsedChanged();

private:
    int groupCount(const QString &key) const;
    QString keyOf(const QModelIndex &index) const;
    void scheduleApply();
    void applyCollapsed();
    bool m_applyPending = false;
    QSet<QString> m_collapsed;

    bool m_grouping = false;
    mutable QHash<QString, int> m_counts;
    mutable bool m_countsDirty = true;
};
