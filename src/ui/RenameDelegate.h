#pragma once

#include <QStyledItemDelegate>

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

Q_SIGNALS:
    void travelRequested(int delta);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    mutable int m_open = 0;
};
