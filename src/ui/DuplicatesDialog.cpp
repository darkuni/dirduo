#include "ui/DuplicatesDialog.h"

#include "core/CommandEngine.h"
#include "core/FileOps.h"
#include "ui/Style.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

DuplicatesDialog::DuplicatesDialog(CommandEngine &engine, const QStringList &folders, const DuplicateOptions &o, QWidget *parent)
    : QDialog(parent)
    , m_engine(engine)
{
    setWindowTitle(QStringLiteral("Find Duplicates"));
    auto *layout = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    m_folders = new QListWidget(this);
    m_folders->addItems(folders);
    m_folders->setMaximumHeight(m_folders->fontMetrics().lineSpacing() * 4 + 8);
    top->addWidget(m_folders, 1);
    auto *fb = new QVBoxLayout;
    auto *add = new QPushButton(QStringLiteral("Add Folder..."), this);
    auto *remove = new QPushButton(QStringLiteral("Remove"), this);
    fb->addWidget(add);
    fb->addWidget(remove);
    fb->addStretch(1);
    top->addLayout(fb);
    layout->addWidget(new QLabel(QStringLiteral("Look in these folders and everything below them:"), this));
    layout->addLayout(top);
    connect(add, &QPushButton::clicked, this, [this]() {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Add a Folder"));
        if (!d.isEmpty())
            m_folders->addItem(d);
    });
    connect(remove, &QPushButton::clicked, this, [this]() { delete m_folders->currentItem(); });

    auto *form = new QFormLayout;
    m_by = new QComboBox(this);
    m_by->addItem(QStringLiteral("Contents (exact, finds renamed copies)"));
    m_by->addItem(QStringLiteral("Name and size"));
    m_by->addItem(QStringLiteral("Name only"));
    m_by->setCurrentIndex(int(o.by));
    form->addRow(QStringLiteral("Same when:"), m_by);
    auto *opts = new QHBoxLayout;
    m_minSize = new QSpinBox(this);
    m_minSize->setRange(0, 4 * 1024 * 1024);
    m_minSize->setSuffix(QStringLiteral(" KB"));
    m_minSize->setValue(int(o.minSize / 1024));
    m_minSize->setSpecialValueText(QStringLiteral("any size"));
    m_hidden = new QCheckBox(QStringLiteral("Include hidden files"), this);
    m_hidden->setChecked(o.includeHidden);
    opts->addWidget(new QLabel(QStringLiteral("At least"), this));
    opts->addWidget(m_minSize);
    opts->addSpacing(12);
    opts->addWidget(m_hidden);
    opts->addStretch(1);
    m_find = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-find")), QStringLiteral("Find"), this);
    opts->addWidget(m_find);
    form->addRow(QString(), opts);
    layout->addLayout(form);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({QStringLiteral("File"), QStringLiteral("Size"), QStringLiteral("Modified")});
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(false);
    layout->addWidget(m_tree, 1);

    auto *checkRow = new QHBoxLayout;
    auto *mark = new QPushButton(QStringLiteral("Check Copies"), this);
    auto *markMenu = new QMenu(mark);
    connect(markMenu->addAction(QStringLiteral("All but the newest in each group")), &QAction::triggered, this,
            [this]() { checkAllBut(Keep::Newest); });
    connect(markMenu->addAction(QStringLiteral("All but the oldest in each group")), &QAction::triggered, this,
            [this]() { checkAllBut(Keep::Oldest); });
    connect(markMenu->addAction(QStringLiteral("All but the shortest path in each group")), &QAction::triggered, this,
            [this]() { checkAllBut(Keep::ShortestPath); });
    connect(markMenu->addAction(QStringLiteral("All but the first in each group")), &QAction::triggered, this,
            [this]() { checkAllBut(Keep::First); });
    markMenu->addSeparator();
    connect(markMenu->addAction(QStringLiteral("Uncheck All")), &QAction::triggered, this, &DuplicatesDialog::uncheckAll);
    mark->setMenu(markMenu);
    checkRow->addWidget(mark);
    checkRow->addStretch(1);
    m_trash = new QPushButton(QIcon::fromTheme(QStringLiteral("user-trash")), QStringLiteral("Move Checked to Trash"), this);
    m_trash->setEnabled(false);
    checkRow->addWidget(m_trash);
    layout->addLayout(checkRow);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_find, &QPushButton::clicked, this, [this]() {
        if (isSearching() && m_progress)
            m_progress->cancel = true;
        else
            startSearch();
    });
    connect(m_trash, &QPushButton::clicked, this, [this]() {
        const QStringList paths = checkedPaths();
        if (QMessageBox::question(this, QStringLiteral("Move to Trash"),
                                  QStringLiteral("Move %1 checked files to the trash?").arg(paths.size()))
            == QMessageBox::Yes)
            trashChecked();
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item) {
        if (m_filling || !item->parent())
            return;
        // Never every copy of a group.
        QTreeWidgetItem *group = item->parent();
        int checked = 0;
        for (int i = 0; i < group->childCount(); ++i)
            checked += group->child(i)->checkState(0) == Qt::Checked ? 1 : 0;
        if (checked == group->childCount()) {
            m_filling = true;
            item->setCheckState(0, Qt::Unchecked);
            m_filling = false;
        }
        updateSummary();
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (item->parent())
            Q_EMIT showRequested(item->data(0, Qt::UserRole).toString());
    });
    connect(&m_watcher, &QFutureWatcher<QList<DuplicateGroup>>::finished, this, [this]() {
        m_tick.stop();
        m_busy = false;
        m_find->setText(QStringLiteral("Find"));
        const bool canceled = m_progress && m_progress->cancel.load();
        m_groups = canceled ? QList<DuplicateGroup>() : m_watcher.result();
        fill();
        if (canceled)
            m_status->setText(QStringLiteral("Search stopped."));
    });
    m_tick.setInterval(200);
    connect(&m_tick, &QTimer::timeout, this, [this]() {
        if (m_progress)
            m_status->setText(QStringLiteral("%1... %2 files")
                                  .arg(m_progress->phase.load() ? QStringLiteral("Comparing contents") : QStringLiteral("Looking"))
                                  .arg(m_progress->files.load()));
    });
    resize(760, 560);
}

DuplicatesDialog::~DuplicatesDialog()
{
    if (m_progress)
        m_progress->cancel = true;
    m_watcher.waitForFinished();
}

DuplicateOptions DuplicatesDialog::options() const
{
    DuplicateOptions o;
    o.by = static_cast<DuplicateOptions::By>(m_by->currentIndex());
    o.minSize = qMax<qint64>(1, qint64(m_minSize->value()) * 1024);
    o.includeHidden = m_hidden->isChecked();
    return o;
}

void DuplicatesDialog::startSearch()
{
    QStringList roots;
    for (int i = 0; i < m_folders->count(); ++i) {
        const QString d = m_folders->item(i)->text();
        if (QFileInfo(d).isDir())
            roots << d;
    }
    if (roots.isEmpty()) {
        m_status->setText(QStringLiteral("Add a folder to look in (servers are not supported here yet)."));
        return;
    }
    if (m_progress)
        m_progress->cancel = true;
    m_watcher.waitForFinished();
    m_progress = std::make_shared<ScanProgress>();
    const DuplicateOptions o = options();
    auto progress = m_progress;
    m_watcher.setFuture(QtConcurrent::run([roots, o, progress]() { return Duplicates::find(roots, o, progress.get()); }));
    m_busy = true;
    m_find->setText(QStringLiteral("Stop"));
    m_tree->clear();
    m_trash->setEnabled(false);
    m_tick.start();
}

void DuplicatesDialog::fill()
{
    m_filling = true;
    m_tree->setUpdatesEnabled(false);
    m_tree->clear();
    const Theme &t = Style::instance()->theme();
    for (const DuplicateGroup &g : std::as_const(m_groups)) {
        auto *group = new QTreeWidgetItem(m_tree);
        group->setText(0, QStringLiteral("%1 copies of %2").arg(g.paths.size()).arg(QFileInfo(g.paths.first()).fileName()));
        group->setText(1, FileOps::formatSize(g.size));
        group->setFirstColumnSpanned(false);
        QFont bold = group->font(0);
        bold.setBold(true);
        group->setFont(0, bold);
        group->setBackground(0, t.groupHeader);
        group->setBackground(1, t.groupHeader);
        group->setBackground(2, t.groupHeader);
        group->setFlags(Qt::ItemIsEnabled);
        for (const QString &p : g.paths) {
            const QFileInfo fi(p);
            auto *row = new QTreeWidgetItem(group, {p, FileOps::formatSize(fi.size()), QLocale().toString(fi.lastModified(), QLocale::ShortFormat)});
            row->setData(0, Qt::UserRole, p);
            row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            row->setCheckState(0, Qt::Unchecked);
        }
    }
    m_tree->expandAll();
    m_tree->setUpdatesEnabled(true);
    m_filling = false;
    updateSummary();
}

void DuplicatesDialog::checkAllBut(Keep keep)
{
    m_filling = true;
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        int keepIndex = 0;
        for (int i = 1; i < group->childCount(); ++i) {
            const QString p = group->child(i)->data(0, Qt::UserRole).toString();
            const QString k = group->child(keepIndex)->data(0, Qt::UserRole).toString();
            const QDateTime pt = QFileInfo(p).lastModified();
            const QDateTime kt = QFileInfo(k).lastModified();
            bool better = false;
            switch (keep) {
            case Keep::Newest:
                better = pt > kt;
                break;
            case Keep::Oldest:
                better = pt < kt;
                break;
            case Keep::ShortestPath:
                better = p.size() < k.size();
                break;
            case Keep::First:
                break;
            }
            if (better)
                keepIndex = i;
        }
        for (int i = 0; i < group->childCount(); ++i)
            group->child(i)->setCheckState(0, i == keepIndex ? Qt::Unchecked : Qt::Checked);
    }
    m_filling = false;
    updateSummary();
}

void DuplicatesDialog::uncheckAll()
{
    m_filling = true;
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i)
            group->child(i)->setCheckState(0, Qt::Unchecked);
    }
    m_filling = false;
    updateSummary();
}

QStringList DuplicatesDialog::checkedPaths() const
{
    QStringList out;
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i) {
            if (group->child(i)->checkState(0) == Qt::Checked)
                out << group->child(i)->data(0, Qt::UserRole).toString();
        }
    }
    return out;
}

QString DuplicatesDialog::summary() const
{
    return m_status->text();
}

void DuplicatesDialog::updateSummary()
{
    if (m_groups.isEmpty()) {
        m_status->setText(QStringLiteral("No duplicates found."));
        m_trash->setEnabled(false);
        return;
    }
    const QStringList checked = checkedPaths();
    qint64 bytes = 0;
    for (const QString &p : checked)
        bytes += QFileInfo(p).size();
    int files = 0;
    for (const DuplicateGroup &g : std::as_const(m_groups))
        files += int(g.paths.size());
    m_status->setText(QStringLiteral("%1 groups, %2 files; %3 could be freed. %4 checked (%5).")
                          .arg(m_groups.size())
                          .arg(files)
                          .arg(FileOps::formatSize(Duplicates::reclaimable(m_groups)))
                          .arg(checked.size())
                          .arg(FileOps::formatSize(bytes)));
    m_trash->setEnabled(!checked.isEmpty());
}

int DuplicatesDialog::trashChecked()
{
    const QStringList paths = checkedPaths();
    if (paths.isEmpty())
        return 0;
    m_engine.trashItems(paths);
    // Take them out of the list; groups left with one file are no longer duplicates.
    for (DuplicateGroup &g : m_groups) {
        for (const QString &p : paths)
            g.paths.removeAll(p);
    }
    m_groups.erase(std::remove_if(m_groups.begin(), m_groups.end(), [](const DuplicateGroup &g) { return g.paths.size() < 2; }),
                   m_groups.end());
    fill();
    return int(paths.size());
}
