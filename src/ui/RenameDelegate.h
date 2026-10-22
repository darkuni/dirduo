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

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;

Q_SIGNALS:
    void travelRequested(int delta);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    bool isLeader(const QModelIndex &index) const;
    QRect checkRect(const QStyleOptionViewItem &option) const;
    QRect contentRect(const QStyleOptionViewItem &option, const QModelIndex &index) const;

    mutable int m_open = 0;
    QPointer<GroupTreeView> m_groupView;
    bool m_checkMode = false;
    QPointer<QItemSelectionModel> m_selection;
};
