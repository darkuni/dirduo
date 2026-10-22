#include "ui/HistoryDialog.h"

#include "core/CommandEngine.h"
#include "core/History.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

HistoryDialog::HistoryDialog(CommandEngine &engine, Page page, QWidget *parent)
    : QDialog(parent)
    , m_engine(engine)
{
    setWindowTitle(QStringLiteral("History"));
    auto *layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs, 1);

    // Undo
    auto *undoPage = new QWidget(m_tabs);
    auto *ul = new QVBoxLayout(undoPage);
    ul->addWidget(new QLabel(QStringLiteral("Newest first. Undoing something adds it here too, so it can be undone in turn."), undoPage));
    m_undo = new QListWidget(undoPage);
    m_undo->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ul->addWidget(m_undo, 1);
    auto *ub = new QHBoxLayout;
    ub->addStretch(1);
    m_undoButton = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")), QStringLiteral("Undo Selected"), undoPage);
    ub->addWidget(m_undoButton);
    ul->addLayout(ub);
    m_tabs->addTab(undoPage, QStringLiteral("Undo"));

    // File log
    auto *logPage = new QWidget(m_tabs);
    auto *ll = new QVBoxLayout(logPage);
    m_filter = new QLineEdit(logPage);
    m_filter->setPlaceholderText(QStringLiteral("Find a file or folder in the log (name or part of a path)"));
    m_filter->setClearButtonEnabled(true);
    ll->addWidget(m_filter);
    m_log = new QTreeWidget(logPage);
    m_log->setRootIsDecorated(false);
    m_log->setUniformRowHeights(true);
    m_log->setHeaderLabels({QStringLiteral("When"), QStringLiteral("What"), QStringLiteral("Item"), QStringLiteral("To"), QStringLiteral("Note")});
    m_log->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_log->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_log->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_log->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_log->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_log->header()->setStretchLastSection(false);
    ll->addWidget(m_log, 1);
    auto *lb = new QHBoxLayout;
    m_logStatus = new QLabel(logPage);
    lb->addWidget(m_logStatus, 1);
    auto *copy = new QPushButton(QStringLiteral("Copy"), logPage);
    auto *save = new QPushButton(QStringLiteral("Save As..."), logPage);
    auto *clear = new QPushButton(QStringLiteral("Clear Log"), logPage);
    lb->addWidget(copy);
    lb->addWidget(save);
    lb->addWidget(clear);
    ll->addLayout(lb);
    m_tabs->addTab(logPage, QStringLiteral("File Log"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_undoButton, &QPushButton::clicked, this, &HistoryDialog::undoSelected);
    connect(m_undo, &QListWidget::itemDoubleClicked, this, &HistoryDialog::undoSelected);
    connect(m_undo, &QListWidget::itemSelectionChanged, this, [this]() { m_undoButton->setEnabled(!m_undo->selectedItems().isEmpty()); });
    connect(&m_engine.history(), &UndoHistory::changed, this, [this]() {
        fillUndo();
        fillLog();
    });
    connect(m_filter, &QLineEdit::textChanged, this, &HistoryDialog::applyFilter);
    auto visibleEntries = [this]() {
        QList<FileLog::Entry> out;
        for (int i = 0; i < m_log->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = m_log->topLevelItem(i);
            if (it->isHidden())
                continue;
            FileLog::Entry e;
            e.when = it->data(0, Qt::UserRole).toDateTime();
            e.action = it->text(1);
            e.item = it->text(2);
            e.to = it->text(3);
            e.note = it->text(4);
            out << e;
        }
        return out;
    };
    connect(copy, &QPushButton::clicked, this, [visibleEntries]() {
        QApplication::clipboard()->setText(FileLog::toText(visibleEntries()));
    });
    connect(save, &QPushButton::clicked, this, [this, visibleEntries]() {
        const QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Save the File Log"), QStringLiteral("file-log.txt"));
        if (file.isEmpty())
            return;
        QSaveFile out(file);
        if (!out.open(QIODevice::WriteOnly) || out.write(FileLog::toText(visibleEntries()).toUtf8()) < 0 || !out.commit())
            QMessageBox::warning(this, QStringLiteral("Save"), out.errorString());
    });
    connect(clear, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QStringLiteral("Clear Log"), QStringLiteral("Forget every entry in the file log?"))
            != QMessageBox::Yes)
            return;
        FileLog::clear();
        fillLog();
    });

    fillUndo();
    fillLog();
    showPage(page);
    resize(860, 520);
}

void HistoryDialog::showPage(Page page)
{
    m_tabs->setCurrentIndex(page == Page::Undo ? 0 : 1);
}

void HistoryDialog::fillUndo()
{
    m_undo->clear();
    for (const UndoEntry &e : m_engine.history().entries()) {
        auto *item = new QListWidgetItem(QStringLiteral("%1   %2").arg(e.when.toString(QStringLiteral("HH:mm:ss")), e.title), m_undo);
        item->setData(Qt::UserRole, e.id);
        QStringList tip;
        for (int i = 0; i < e.items.size() && i < 15; ++i)
            tip << QStringLiteral("%1  ->  %2").arg(e.items.at(i).first, e.items.at(i).second);
        if (e.items.size() > 15)
            tip << QStringLiteral("... and %1 more").arg(e.items.size() - 15);
        item->setToolTip(tip.join(QLatin1Char('\n')));
        item->setIcon(QIcon::fromTheme(e.isUndo ? QStringLiteral("edit-redo") : QStringLiteral("edit-undo")));
    }
    if (m_undo->count() == 0) {
        auto *item = new QListWidgetItem(QStringLiteral("Nothing to undo yet."), m_undo);
        item->setFlags(Qt::NoItemFlags);
    }
    m_undoButton->setEnabled(false);
}

void HistoryDialog::fillLog()
{
    m_log->clear();
    const QList<FileLog::Entry> entries = FileLog::read();
    QList<QTreeWidgetItem *> items;
    // Newest at the top.
    for (int i = int(entries.size()) - 1; i >= 0; --i) {
        const FileLog::Entry &e = entries.at(i);
        auto *it = new QTreeWidgetItem({e.when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), e.action, e.item, e.to, e.note});
        it->setData(0, Qt::UserRole, e.when);
        it->setToolTip(2, e.item);
        it->setToolTip(3, e.to);
        items << it;
    }
    m_log->addTopLevelItems(items);
    applyFilter();
}

void HistoryDialog::setLogFilter(const QString &text)
{
    m_filter->setText(text);
}

int HistoryDialog::visibleLogRows() const
{
    int n = 0;
    for (int i = 0; i < m_log->topLevelItemCount(); ++i)
        n += m_log->topLevelItem(i)->isHidden() ? 0 : 1;
    return n;
}

void HistoryDialog::applyFilter()
{
    const QString f = m_filter->text().trimmed();
    for (int i = 0; i < m_log->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = m_log->topLevelItem(i);
        it->setHidden(!f.isEmpty() && !it->text(2).contains(f, Qt::CaseInsensitive) && !it->text(3).contains(f, Qt::CaseInsensitive));
    }
    const int shown = visibleLogRows();
    m_logStatus->setText(f.isEmpty() ? QStringLiteral("%1 entries (the last %2 are kept)").arg(shown).arg(FileLog::kKeep)
                                     : QStringLiteral("%1 of %2 entries").arg(shown).arg(m_log->topLevelItemCount()));
}

void HistoryDialog::undoSelected()
{
    // In list order (newest first), one at a time.
    QList<int> ids;
    for (int i = 0; i < m_undo->count(); ++i) {
        if (m_undo->item(i)->isSelected() && m_undo->item(i)->data(Qt::UserRole).isValid())
            ids << m_undo->item(i)->data(Qt::UserRole).toInt();
    }
    QStringList touched;
    QStringList problems;
    for (int id : std::as_const(ids)) {
        QString err;
        m_engine.history().undo(id, &touched, &err);
        if (!err.isEmpty())
            problems << err;
    }
    if (!touched.isEmpty())
        Q_EMIT undone(touched);
    if (!problems.isEmpty())
        QMessageBox::warning(this, QStringLiteral("Undo"), problems.join(QLatin1Char('\n')));
}
