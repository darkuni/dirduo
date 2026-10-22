#include "ui/RenameDelegate.h"

#include "ui/FileListModel.h"
#include "ui/GroupTreeView.h"
#include "ui/Style.h"

#include <QApplication>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QPainter>

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

void RenameDelegate::setCheckMode(bool on, QItemSelectionModel *selection)
{
    m_checkMode = on;
    m_selection = selection;
}

bool RenameDelegate::isLeader(const QModelIndex &index) const
{
    return m_groupView && m_groupView->isGroupLeader(index);
}

QRect RenameDelegate::contentRect(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QRect r = option.rect;
    if (isLeader(index) && r.height() > m_groupView->bandHeight())
        r.setTop(r.top() + m_groupView->bandHeight());
    return r;
}

QRect RenameDelegate::checkRect(const QStyleOptionViewItem &option) const
{
    const QStyle *style = option.widget ? option.widget->style() : QApplication::style();
    const int w = style->pixelMetric(QStyle::PM_IndicatorWidth, &option, option.widget);
    const int h = style->pixelMetric(QStyle::PM_IndicatorHeight, &option, option.widget);
    return QRect(option.rect.left() + 4, option.rect.center().y() - h / 2, w, h);
}

QSize RenameDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize s = QStyledItemDelegate::sizeHint(option, index);
    if (m_checkMode && index.column() == FileListModel::NameColumn)
        s.rwidth() += 24;
    if (isLeader(index)) {
        if (m_groupView->isCollapsed(index))
            return QSize(s.width(), m_groupView->bandHeight()); // heading only
        s.rheight() += m_groupView->bandHeight();
    }
    return s;
}

void RenameDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem opt = option;
    // GroupTreeView already moved the row below its band; nothing to shift.

    // Live folder sizes: a bar behind the size, scaled to the biggest folder.
    if (index.column() == FileListModel::SizeColumn) {
        const double share = index.data(FileListModel::FolderBarRole).toDouble();
        if (share >= 0.0) {
            QColor bar = Style::instance()->theme().sizeBar;
            bar.setAlphaF(0.55);
            const QRect area = opt.rect.adjusted(3, 3, -3, -3);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(bar);
            painter->drawRoundedRect(QRectF(area.left(), area.top(), qMax(2.0, area.width() * share), area.height()), 3, 3);
            painter->restore();
        }
    }

    if (m_checkMode && index.column() == FileListModel::NameColumn) {
        QStyleOptionViewItem box = opt;
        box.rect = checkRect(opt);
        box.state &= ~QStyle::State_HasFocus;
        const bool checked = m_selection && m_selection->isRowSelected(index.row(), index.parent());
        box.state |= checked ? QStyle::State_On : QStyle::State_Off;
        const QWidget *w = opt.widget;
        // Paint the row background first so the box sits on it.
        QStyleOptionViewItem bg = opt;
        bg.text.clear();
        bg.icon = QIcon();
        (w ? w->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &bg, painter, w);
        (w ? w->style() : QApplication::style())->drawPrimitive(QStyle::PE_IndicatorItemViewItemCheck, &box, painter, w);
        opt.rect.setLeft(box.rect.right() + 6);
    }
    QStyledItemDelegate::paint(painter, opt, index);
}

void RenameDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem opt = option;
    opt.rect = contentRect(option, index);
    if (m_checkMode && index.column() == FileListModel::NameColumn)
        opt.rect.setLeft(checkRect(opt).right() + 6);
    QStyledItemDelegate::updateEditorGeometry(editor, opt, index);
}

bool RenameDelegate::editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                                 const QModelIndex &index)
{
    if (m_checkMode && m_selection && index.column() == FileListModel::NameColumn
        && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::MouseButtonDblClick)) {
        auto *me = static_cast<QMouseEvent *>(event);
        QStyleOptionViewItem opt = option;
        opt.rect = contentRect(option, index);
        if (checkRect(opt).adjusted(-3, -3, 3, 3).contains(me->position().toPoint())) {
            if (event->type() == QEvent::MouseButtonPress && me->button() == Qt::LeftButton) {
                m_selection->select(index, QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
                m_selection->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
            }
            return true; // the box takes the click; the rest of the selection stays
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}
