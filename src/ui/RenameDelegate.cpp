#include "ui/RenameDelegate.h"

#include "ui/FileListModel.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <QTimer>

QWidget *RenameDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QWidget *w = QStyledItemDelegate::createEditor(parent, option, index);
    if (auto *edit = qobject_cast<QLineEdit *>(w))
        edit->setFrame(true);
    if (w)
        ++m_open;
    return w;
}

void RenameDelegate::destroyEditor(QWidget *editor, const QModelIndex &index) const
{
    if (m_open > 0)
        --m_open;
    QStyledItemDelegate::destroyEditor(editor, index);
}

void RenameDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    QStyledItemDelegate::setEditorData(editor, index);
    auto *edit = qobject_cast<QLineEdit *>(editor);
    if (!edit)
        return;
    const QString name = edit->text();
    const bool isDir = index.data(FileListModel::IsDirRole).toBool();
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (isDir || dot <= 0) {
        edit->selectAll();
        return;
    }
    // The view selects all text after this returns, so select the name part
    // once the editor is up.
    edit->setSelection(0, dot);
    QPointer<QLineEdit> guard(edit);
    QTimer::singleShot(0, edit, [guard, dot]() {
        if (guard)
            guard->setSelection(0, dot);
    });
}

bool RenameDelegate::eventFilter(QObject *object, QEvent *event)
{
    auto *editor = qobject_cast<QWidget *>(object);
    if (!editor)
        return QStyledItemDelegate::eventFilter(object, event);

    auto travelKey = [](int key, Qt::KeyboardModifiers mods) -> int {
        if (key == Qt::Key_Down || (key == Qt::Key_Tab && !(mods & Qt::ShiftModifier)))
            return 1;
        if (key == Qt::Key_Up || key == Qt::Key_Backtab || (key == Qt::Key_Tab && (mods & Qt::ShiftModifier)))
            return -1;
        return 0;
    };

    if (event->type() == QEvent::ShortcutOverride) {
        // Keep window shortcuts (Tab, arrows) from firing while renaming.
        auto *ke = static_cast<QKeyEvent *>(event);
        if (travelKey(ke->key(), ke->modifiers()) != 0) {
            event->accept();
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const int delta = travelKey(ke->key(), ke->modifiers());
        if (delta != 0) {
            Q_EMIT commitData(editor);
            Q_EMIT closeEditor(editor, QAbstractItemDelegate::NoHint);
            Q_EMIT travelRequested(delta);
            return true;
        }
    }
    return QStyledItemDelegate::eventFilter(object, event);
}
