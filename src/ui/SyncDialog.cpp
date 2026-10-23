#include "ui/SyncDialog.h"

#include "core/CommandEngine.h"
#include "core/FileOps.h"
#include "ui/Style.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {

constexpr int kEntryRole = Qt::UserRole + 1;

QString sideText(qint64 size, const QDateTime &time, bool isDir)
{
    if (!time.isValid())
        return QString();
    const QString when = QLocale().toString(time, QLocale::ShortFormat);
    if (isDir || size < 0)
        return QStringLiteral("folder, %1").arg(when);
    return QStringLiteral("%1, %2").arg(FileOps::formatSize(size), when);
}

} // namespace

SyncDialog::SyncDialog(CommandEngine &engine, const QString &left, const QString &right, const SyncOptions &o, QWidget *parent)
    : QDialog(parent)
    , m_engine(engine)
{
    setWindowTitle(QStringLiteral("Synchronize"));
    auto *layout = new QVBoxLayout(this);
    auto *grid = new QGridLayout;
    auto folderRow = [this, grid](int row, const QString &label, const QString &value) {
        auto *edit = new QLineEdit(value, this);
        auto *browse = new QToolButton(this);
        browse->setText(QStringLiteral("..."));
        connect(browse, &QToolButton::clicked, this, [this, edit]() {
            const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose a Folder"), edit->text());
            if (!d.isEmpty())
                edit->setText(d);
        });
        grid->addWidget(new QLabel(label, this), row, 0);
        grid->addWidget(edit, row, 1);
        grid->addWidget(browse, row, 2);
        return edit;
    };
    m_left = folderRow(0, QStringLiteral("Left:"), left);
    m_right = folderRow(1, QStringLiteral("Right:"), right);
    auto *swap = new QToolButton(this);
    swap->setText(QStringLiteral("Swap"));
    swap->setToolTip(QStringLiteral("Swap left and right"));
    grid->addWidget(swap, 0, 3, 2, 1);
    connect(swap, &QToolButton::clicked, this, [this]() {
        const QString l = m_left->text();
        m_left->setText(m_right->text());
        m_right->setText(l);
    });

    m_mode = new QComboBox(this);
    m_mode->addItem(QStringLiteral("Update the right: copy new and newer files, never delete"));
    m_mode->addItem(QStringLiteral("Mirror to the right: make it an exact copy (extra files go to the trash)"));
    m_mode->addItem(QStringLiteral("Both ways: copy each side's new and newer files to the other"));
    m_mode->setCurrentIndex(int(o.mode));
    grid->addWidget(new QLabel(QStringLiteral("Mode:"), this), 2, 0);
    grid->addWidget(m_mode, 2, 1, 1, 3);
    m_compare = new QComboBox(this);
    m_compare->addItem(QStringLiteral("Date"));
    m_compare->addItem(QStringLiteral("Size"));
    m_compare->addItem(QStringLiteral("Date and size"));
    m_compare->addItem(QStringLiteral("Contents (reads every file the same size; slow, exact)"));
    m_compare->setCurrentIndex(int(o.compareBy));
    grid->addWidget(new QLabel(QStringLiteral("Compare by:"), this), 3, 0);
    grid->addWidget(m_compare, 3, 1, 1, 3);
    m_exclude = new QLineEdit(o.exclude.join(QLatin1Char(';')), this);
    m_exclude->setPlaceholderText(QStringLiteral("Names to leave out, for example *.tmp;Thumbs.db;.DS_Store"));
    grid->addWidget(new QLabel(QStringLiteral("Leave out:"), this), 4, 0);
    grid->addWidget(m_exclude, 4, 1, 1, 3);
    layout->addLayout(grid);

    auto *compareRow = new QHBoxLayout;
    m_compareButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), QStringLiteral("Compare"), this);
    m_showSame = new QCheckBox(QStringLiteral("Show identical files"), this);
    compareRow->addWidget(m_compareButton);
    compareRow->addStretch(1);
    compareRow->addWidget(m_showSame);
    layout->addLayout(compareRow);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Left"), QStringLiteral("Action"), QStringLiteral("Right")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(false);
    layout->addWidget(m_tree, 1);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_sync = buttons->addButton(QStringLiteral("Synchronize"), QDialogButtonBox::AcceptRole);
    m_sync->setEnabled(false);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_sync, &QPushButton::clicked, this, [this]() {
        if (synchronize() > 0)
            accept();
    });
    layout->addWidget(buttons);

    connect(m_compareButton, &QPushButton::clicked, this, [this]() {
        if (isComparing() && m_progress)
            m_progress->cancel = true;
        else
            startCompare();
    });
    connect(m_showSame, &QCheckBox::toggled, this, &SyncDialog::fill);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &SyncDialog::showRowMenu);
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this]() {
        // New mode, new default actions; the comparison itself still holds.
        for (SyncEntry &e : m_entries) {
            if (!(e.isDir && e.leftTime.isValid() && e.rightTime.isValid()))
                e.action = FolderSync::defaultAction(e.state, options().mode);
        }
        fill();
    });
    connect(&m_watcher, &QFutureWatcher<QList<SyncEntry>>::finished, this, [this]() {
        m_tick.stop();
        m_busy = false;
        m_compareButton->setText(QStringLiteral("Compare"));
        const bool canceled = m_progress && m_progress->cancel.load();
        m_entries = canceled ? QList<SyncEntry>() : m_watcher.result();
        fill();
        if (canceled)
            m_status->setText(QStringLiteral("Comparison stopped."));
    });
    m_tick.setInterval(200);
    connect(&m_tick, &QTimer::timeout, this, [this]() {
        if (m_progress)
            m_status->setText(QStringLiteral("%1... %2 items checked")
                                  .arg(m_progress->phase.load() ? QStringLiteral("Comparing contents") : QStringLiteral("Comparing"))
                                  .arg(m_progress->files.load()));
    });
    resize(820, 560);
}

SyncDialog::~SyncDialog()
{
    if (m_progress)
        m_progress->cancel = true;
    m_watcher.waitForFinished();
}

SyncOptions SyncDialog::options() const
{
    SyncOptions o;
    o.mode = static_cast<SyncOptions::Mode>(m_mode->currentIndex());
    o.compareBy = static_cast<SyncOptions::CompareBy>(m_compare->currentIndex());
    o.exclude = m_exclude->text().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    return o;
}

void SyncDialog::startCompare()
{
    const QString l = m_left->text().trimmed();
    const QString r = m_right->text().trimmed();
    if (!QFileInfo(l).isDir() || !QFileInfo(r).isDir()) {
        m_status->setText(QStringLiteral("Both sides need to be folders that exist (servers are not supported here yet)."));
        return;
    }
    if (QFileInfo(l).canonicalFilePath() == QFileInfo(r).canonicalFilePath()) {
        m_status->setText(QStringLiteral("Left and right are the same folder."));
        return;
    }
    if (m_progress)
        m_progress->cancel = true;
    m_watcher.waitForFinished();
    m_progress = std::make_shared<ScanProgress>();
    m_comparedLeft = l;
    m_comparedRight = r;
    const SyncOptions o = options();
    auto progress = m_progress;
    m_watcher.setFuture(QtConcurrent::run([l, r, o, progress]() { return FolderSync::compare(l, r, o, progress.get()); }));
    m_busy = true;
    m_compareButton->setText(QStringLiteral("Stop"));
    m_sync->setEnabled(false);
    m_tree->clear();
    m_tick.start();
}

void SyncDialog::fill()
{
    m_tree->setUpdatesEnabled(false);
    m_tree->clear();
    for (int i = 0; i < m_entries.size(); ++i) {
        const SyncEntry &e = m_entries.at(i);
        if (e.state == SyncEntry::State::Same && !m_showSame->isChecked())
            continue;
        auto *row = new QTreeWidgetItem(m_tree);
        row->setData(0, kEntryRole, i);
        row->setIcon(0, QIcon::fromTheme(e.isDir ? QStringLiteral("folder") : QStringLiteral("text-x-generic")));
        updateRow(row);
    }
    m_tree->setUpdatesEnabled(true);
    updateSummary();
}

void SyncDialog::updateRow(QTreeWidgetItem *row)
{
    const SyncEntry &e = m_entries.at(row->data(0, kEntryRole).toInt());
    row->setText(0, e.rel);
    row->setText(1, sideText(e.leftSize, e.leftTime, e.isDir));
    row->setText(3, sideText(e.rightSize, e.rightTime, e.isDir));
    row->setText(2, FolderSync::actionText(e.action));
    row->setToolTip(2, FolderSync::stateText(e.state));
    row->setToolTip(0, FolderSync::stateText(e.state));
    const Theme &t = Style::instance()->theme();
    QColor c = t.dimText;
    switch (e.action) {
    case SyncEntry::Action::CopyToRight:
    case SyncEntry::Action::CopyToLeft:
        c = t.sourceAccent;
        break;
    case SyncEntry::Action::DeleteLeft:
    case SyncEntry::Action::DeleteRight:
        c = QColor::fromRgb(0xd0, 0x40, 0x40);
        break;
    default:
        break;
    }
    row->setForeground(2, c);
    QFont f = row->font(2);
    f.setBold(e.action != SyncEntry::Action::None);
    row->setFont(2, f);
    // Newer side in bold, so "why" is visible at a glance.
    QFont lf = row->font(1);
    lf.setBold(e.state == SyncEntry::State::LeftNewer);
    row->setFont(1, lf);
    QFont rf = row->font(3);
    rf.setBold(e.state == SyncEntry::State::RightNewer);
    row->setFont(3, rf);
}

QString SyncDialog::summary() const
{
    return m_status->text();
}

void SyncDialog::updateSummary()
{
    int same = 0;
    int conflicts = 0;
    for (const SyncEntry &e : std::as_const(m_entries)) {
        same += e.state == SyncEntry::State::Same ? 1 : 0;
        conflicts += (e.state != SyncEntry::State::Same && e.action == SyncEntry::Action::None) ? 1 : 0;
    }
    const FolderSync::Plan p = FolderSync::plan(m_entries, m_comparedLeft, m_comparedRight);
    int copies = 0;
    for (const QStringList &l : p.copies)
        copies += int(l.size());
    QString text;
    if (m_entries.isEmpty() || (p.items == 0 && conflicts == 0))
        text = QStringLiteral("The folders match (%1 identical files).").arg(same);
    else
        text = QStringLiteral("%1 to copy (%2), %3 to delete, %4 skipped, %5 identical.")
                   .arg(copies)
                   .arg(FileOps::formatSize(p.bytes))
                   .arg(p.deletes.size())
                   .arg(conflicts)
                   .arg(same);
    if (conflicts)
        text += QStringLiteral(" Right-click skipped items to choose what happens to them.");
    m_status->setText(text);
    m_sync->setEnabled(p.items > 0);
}

void SyncDialog::setAction(int entryIndex, SyncEntry::Action action)
{
    if (entryIndex < 0 || entryIndex >= m_entries.size())
        return;
    SyncEntry &e = m_entries[entryIndex];
    // Only what makes sense for this entry.
    const bool hasL = e.leftTime.isValid();
    const bool hasR = e.rightTime.isValid();
    using A = SyncEntry::Action;
    if ((action == A::CopyToRight || action == A::DeleteLeft) && !hasL)
        return;
    if ((action == A::CopyToLeft || action == A::DeleteRight) && !hasR)
        return;
    e.action = action;
    for (int r = 0; r < m_tree->topLevelItemCount(); ++r) {
        if (m_tree->topLevelItem(r)->data(0, kEntryRole).toInt() == entryIndex)
            updateRow(m_tree->topLevelItem(r));
    }
    updateSummary();
}

void SyncDialog::showRowMenu(const QPoint &pos)
{
    const QList<QTreeWidgetItem *> rows = m_tree->selectedItems();
    if (rows.isEmpty())
        return;
    QMenu menu(this);
    auto add = [&](const QString &text, SyncEntry::Action a) {
        connect(menu.addAction(text), &QAction::triggered, this, [this, rows, a]() {
            for (QTreeWidgetItem *r : rows)
                setAction(r->data(0, kEntryRole).toInt(), a);
        });
    };
    add(QStringLiteral("Copy Left to Right"), SyncEntry::Action::CopyToRight);
    add(QStringLiteral("Copy Right to Left"), SyncEntry::Action::CopyToLeft);
    menu.addSeparator();
    add(QStringLiteral("Delete on the Left"), SyncEntry::Action::DeleteLeft);
    add(QStringLiteral("Delete on the Right"), SyncEntry::Action::DeleteRight);
    menu.addSeparator();
    add(QStringLiteral("Skip"), SyncEntry::Action::None);
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

int SyncDialog::synchronize()
{
    const FolderSync::Plan p = FolderSync::plan(m_entries, m_comparedLeft, m_comparedRight);
    if (p.items == 0)
        return 0;
    return FolderSync::run(p, m_engine);
}
