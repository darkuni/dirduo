#pragma once

#include <QPointer>
#include <QStyledItemDelegate>

#include "ui/GroupTreeView.h"
class QItemSelectionModel;

// Inline rename editor. Opens with the name selected up to the extension.
// Up/Down and Tab/Shift+Tab commit and move straight into renaming the next or
// previous item, Opus style.
class RenameDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void destroyEditor(QWidget *editor, const QModelIndex &index) const override;

    bool isEditing() const { return m_open > 0; }

    // Grouping bands live above leader rows of this view (details only).
    void setGroupView(GroupTreeView *view) { m_groupView = view; }
    // Check box mode: a box in front of each name mirrors the selection.
    void setCheckMode(bool on, QItemSelectionModel *selection);
    bool checkMode() const { return m_checkMode; }
    // Details view: indent expanded folders' contents and draw the triangles.
    void setTreeMode(bool on) { m_tree = on; }
    // The triangle of a folder row, for clicks (option as the view gives it).
    QRect expanderRect(const QStyleOptionViewItem &option, const QModelIndex &index) const;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;

Q_SIGNALS:
    void travelRequested(int delta);
    void expandToggled(const QModelIndex &index);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    bool isLeader(const QModelIndex &index) const;
    QRect checkRect(const QStyleOptionViewItem &option) const;
    QRect contentRect(const QStyleOptionViewItem &option, const QModelIndex &index) const;

    mutable int m_open = 0;
    QPointer<GroupTreeView> m_groupView;
    bool m_checkMode = false;
    bool m_tree = false;
    // Room for the indent and the triangle (0 when there is none).
    int treeIndent(const QStyleOptionViewItem &option, const QModelIndex &index) const;
    int treeUnit(const QStyleOptionViewItem &option) const;
    QPointer<QItemSelectionModel> m_selection;
};
