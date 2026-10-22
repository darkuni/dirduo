#include "ui/FindDialog.h"

#include "core/Collections.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {
const int kNewerDays[] = {-1, 0, 7, 30, 365};
}

FindDialog::FindDialog(const QStringList &roots, const FindCriteria &c, const QString &collection, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Find"));
    auto *layout = new QVBoxLayout(this);

    auto *presetRow = new QHBoxLayout;
    presetRow->addWidget(new QLabel(QStringLiteral("Saved search:"), this));
    m_presets = new QComboBox(this);
    presetRow->addWidget(m_presets, 1);
    auto *save = new QPushButton(QStringLiteral("Save..."), this);
    auto *remove = new QPushButton(QStringLiteral("Delete"), this);
    presetRow->addWidget(save);
    presetRow->addWidget(remove);
    layout->addLayout(presetRow);

    auto *form = new QFormLayout;
    auto *nameRow = new QHBoxLayout;
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(QStringLiteral("galaga, *.d64;*.adf, or a regular expression"));
    m_regex = new QCheckBox(QStringLiteral("Regular expression"), this);
    m_case = new QCheckBox(QStringLiteral("Match case"), this);
    nameRow->addWidget(m_name, 1);
    nameRow->addWidget(m_regex);
    nameRow->addWidget(m_case);
    form->addRow(QStringLiteral("Name:"), nameRow);
    m_contains = new QLineEdit(this);
    m_contains->setPlaceholderText(QStringLiteral("Text inside the file (slower)"));
    form->addRow(QStringLiteral("Containing:"), m_contains);
    m_type = new QComboBox(this);
    m_type->addItems({QStringLiteral("Files"), QStringLiteral("Folders"), QStringLiteral("Files and folders")});
    form->addRow(QStringLiteral("Find:"), m_type);
    auto *sizeRow = new QHBoxLayout;
    m_minSize = new QLineEdit(this);
    m_minSize->setPlaceholderText(QStringLiteral("at least, e.g. 10M"));
    m_maxSize = new QLineEdit(this);
    m_maxSize->setPlaceholderText(QStringLiteral("at most, e.g. 700K"));
    sizeRow->addWidget(m_minSize);
    sizeRow->addWidget(m_maxSize);
    form->addRow(QStringLiteral("Size:"), sizeRow);
    auto *dateRow = new QHBoxLayout;
    m_newer = new QComboBox(this);
    m_newer->addItems({QStringLiteral("Any time"), QStringLiteral("Today"), QStringLiteral("In the last 7 days"),
                       QStringLiteral("In the last 30 days"), QStringLiteral("In the last year")});
    m_older = new QLineEdit(this);
    m_older->setPlaceholderText(QStringLiteral("not since: days ago, or 2024-01-31"));
    dateRow->addWidget(m_newer);
    dateRow->addWidget(m_older);
    form->addRow(QStringLiteral("Modified:"), dateRow);
    layout->addLayout(form);

    layout->addWidget(new QLabel(QStringLiteral("Look in:"), this));
    auto *rootRow = new QHBoxLayout;
    m_roots = new QListWidget(this);
    m_roots->addItems(roots);
    m_roots->setMaximumHeight(m_roots->fontMetrics().lineSpacing() * 4 + 8);
    rootRow->addWidget(m_roots, 1);
    auto *rb = new QVBoxLayout;
    auto *add = new QPushButton(QStringLiteral("Add Folder..."), this);
    auto *drop = new QPushButton(QStringLiteral("Remove"), this);
    rb->addWidget(add);
    rb->addWidget(drop);
    rb->addStretch(1);
    rootRow->addLayout(rb);
    layout->addLayout(rootRow);
    auto *opts = new QHBoxLayout;
    m_recurse = new QCheckBox(QStringLiteral("Subfolders"), this);
    m_hidden = new QCheckBox(QStringLiteral("Hidden files"), this);
    opts->addWidget(m_recurse);
    opts->addWidget(m_hidden);
    opts->addStretch(1);
    opts->addWidget(new QLabel(QStringLiteral("Results to collection:"), this));
    m_collection = new QLineEdit(collection.isEmpty() ? QStringLiteral("Find Results") : collection, this);
    opts->addWidget(m_collection);
    layout->addLayout(opts);

    m_status = new QLabel(this);
    layout->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_search = buttons->addButton(QStringLiteral("Find"), QDialogButtonBox::ActionRole);
    m_search->setIcon(QIcon::fromTheme(QStringLiteral("edit-find")));
    m_search->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(add, &QPushButton::clicked, this, [this]() {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Add a Folder"));
        if (!d.isEmpty())
            m_roots->addItem(d);
    });
    connect(drop, &QPushButton::clicked, this, [this]() { delete m_roots->currentItem(); });
    connect(m_search, &QPushButton::clicked, this, [this]() {
        if (m_busy && m_progress)
            m_progress->cancel = true;
        else
            startSearch();
    });
    connect(m_name, &QLineEdit::returnPressed, this, &FindDialog::startSearch);
    connect(m_presets, &QComboBox::activated, this, [this](int i) {
        if (i <= 0)
            return;
        FindCriteria c;
        QStringList roots;
        if (FindPresets::load(m_presets->itemText(i), c, &roots)) {
            setCriteria(c);
            if (!roots.isEmpty()) {
                m_roots->clear();
                m_roots->addItems(roots);
            }
        }
    });
    connect(save, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString current = m_presets->currentIndex() > 0 ? m_presets->currentText() : QString();
        const QString name = QInputDialog::getText(this, QStringLiteral("Save Search"), QStringLiteral("Name:"), QLineEdit::Normal, current, &ok);
        if (ok && FindPresets::save(name, criteria(), this->roots()))
            refreshPresets(name.trimmed());
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        if (m_presets->currentIndex() > 0 && FindPresets::remove(m_presets->currentText()))
            refreshPresets();
    });
    connect(&m_watcher, &QFutureWatcher<QStringList>::finished, this, [this]() {
        m_tick.stop();
        m_busy = false;
        m_search->setText(QStringLiteral("Find"));
        m_results = m_watcher.result();
        const QString name = this->collection();
        Collections::instance()->setItems(name, m_results);
        const bool stopped = m_progress && m_progress->cancel.load();
        m_status->setText(QStringLiteral("%1 found%2%3")
                              .arg(m_results.size())
                              .arg(m_truncated ? QStringLiteral(" (stopped at the limit)") : QString())
                              .arg(stopped ? QStringLiteral(" before you stopped it") : QString()));
        Q_EMIT resultsReady(Collections::location(name), int(m_results.size()));
    });
    m_tick.setInterval(200);
    connect(&m_tick, &QTimer::timeout, this, [this]() {
        if (m_progress)
            m_status->setText(QStringLiteral("Looking... %1 checked").arg(m_progress->files.load()));
    });

    refreshPresets();
    setCriteria(c);
    resize(640, 480);
    m_name->setFocus();
}

FindDialog::~FindDialog()
{
    if (m_progress)
        m_progress->cancel = true;
    m_watcher.waitForFinished();
}

void FindDialog::refreshPresets(const QString &select)
{
    m_presets->clear();
    m_presets->addItem(QStringLiteral("(new search)"));
    m_presets->addItems(FindPresets::names());
    if (!select.isEmpty())
        m_presets->setCurrentText(select);
}

FindCriteria FindDialog::criteria() const
{
    FindCriteria c;
    c.name = m_name->text().trimmed();
    c.nameRegex = m_regex->isChecked();
    c.caseSensitive = m_case->isChecked();
    c.contains = m_contains->text();
    c.type = static_cast<FindCriteria::Type>(m_type->currentIndex());
    c.minSize = m_minSize->text().trimmed().isEmpty() ? -1 : FindCriteria::parseSize(m_minSize->text());
    c.maxSize = m_maxSize->text().trimmed().isEmpty() ? -1 : FindCriteria::parseSize(m_maxSize->text());
    const int days = kNewerDays[qBound(0, m_newer->currentIndex(), 4)];
    if (days >= 0)
        c.newerThan = FindCriteria::parseWhen(QString::number(days));
    c.olderThan = FindCriteria::parseWhen(m_older->text());
    c.recurse = m_recurse->isChecked();
    c.includeHidden = m_hidden->isChecked();
    return c;
}

void FindDialog::setCriteria(const FindCriteria &c)
{
    m_name->setText(c.name);
    m_regex->setChecked(c.nameRegex);
    m_case->setChecked(c.caseSensitive);
    m_contains->setText(c.contains);
    m_type->setCurrentIndex(int(c.type));
    m_minSize->setText(c.minSize >= 0 ? QString::number(c.minSize) : QString());
    m_maxSize->setText(c.maxSize >= 0 ? QString::number(c.maxSize) : QString());
    int newer = 0;
    if (c.newerThan.isValid()) {
        const qint64 d = c.newerThan.date().daysTo(QDate::currentDate());
        for (int i = 1; i < 5; ++i) {
            if (kNewerDays[i] == d)
                newer = i;
        }
    }
    m_newer->setCurrentIndex(newer);
    m_older->setText(c.olderThan.isValid() ? c.olderThan.date().toString(Qt::ISODate) : QString());
    m_recurse->setChecked(c.recurse);
    m_hidden->setChecked(c.includeHidden);
}

QStringList FindDialog::roots() const
{
    QStringList out;
    for (int i = 0; i < m_roots->count(); ++i)
        out << m_roots->item(i)->text();
    return out;
}

QString FindDialog::collection() const
{
    const QString n = m_collection->text().trimmed();
    return n.isEmpty() || n.contains(QLatin1Char('/')) ? QStringLiteral("Find Results") : n;
}

QString FindDialog::status() const
{
    return m_status->text();
}

void FindDialog::startSearch()
{
    QStringList roots;
    for (const QString &r : this->roots()) {
        if (QFileInfo(r).isDir())
            roots << r;
    }
    if (roots.isEmpty()) {
        m_status->setText(QStringLiteral("Add a folder to look in (servers are not supported here yet)."));
        return;
    }
    const FindCriteria c = criteria();
    if ((!m_minSize->text().trimmed().isEmpty() && c.minSize < 0) || (!m_maxSize->text().trimmed().isEmpty() && c.maxSize < 0)) {
        m_status->setText(QStringLiteral("Sizes look like 500, 700K, 10M or 2G."));
        return;
    }
    if (!m_older->text().trimmed().isEmpty() && !c.olderThan.isValid()) {
        m_status->setText(QStringLiteral("\"Not since\" takes a number of days or a date like 2024-01-31."));
        return;
    }
    if (c.nameRegex && !QRegularExpression(c.name).isValid()) {
        m_status->setText(QStringLiteral("The regular expression has an error."));
        return;
    }
    if (m_progress)
        m_progress->cancel = true;
    m_watcher.waitForFinished();
    m_progress = std::make_shared<ScanProgress>();
    auto progress = m_progress;
    m_truncated = false;
    bool *truncated = &m_truncated;
    m_watcher.setFuture(QtConcurrent::run([roots, c, progress, truncated]() {
        return FindEngine::find(roots, c, progress.get(), 100000, truncated);
    }));
    m_busy = true;
    m_search->setText(QStringLiteral("Stop"));
    m_tick.start();
}
