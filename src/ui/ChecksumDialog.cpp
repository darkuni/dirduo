#include "ui/ChecksumDialog.h"

#include "core/Location.h"
#include "core/FileOps.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {

QString commonFolder(const QStringList &paths)
{
    if (paths.isEmpty())
        return {};
    QString base = Location::parent(paths.first());
    for (const QString &p : paths) {
        while (!base.isEmpty() && !Location::isSameOrInside(p, base))
            base = Location::parent(base);
    }
    return base;
}

QString relativeTo(const QString &path, const QString &base)
{
    QString rel = path.mid(base.size());
    while (rel.startsWith(QLatin1Char('/')))
        rel.remove(0, 1);
    return rel;
}

} // namespace

ChecksumDialog::ChecksumDialog(Mode mode, const QStringList &paths, Checksum::Algo algo, QWidget *parent)
    : QDialog(parent)
    , m_mode(mode)
    , m_paths(paths)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    setWindowTitle(mode == Mode::Make ? QStringLiteral("Checksums") : QStringLiteral("Verify Checksums"));
    auto *layout = new QVBoxLayout(this);
    if (mode == Mode::Make) {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(QStringLiteral("&Kind:"), this));
        m_algo = new QComboBox(this);
        for (Checksum::Algo a : Checksum::all())
            m_algo->addItem(Checksum::label(a), int(a));
        m_algo->setCurrentIndex(m_algo->findData(int(algo)));
        qobject_cast<QLabel *>(row->itemAt(0)->widget())->setBuddy(m_algo);
        row->addWidget(m_algo);
        row->addStretch(1);
        layout->addLayout(row);
        connect(m_algo, &QComboBox::currentIndexChanged, this, [this]() { start(); });
    }
    m_list = new QTreeWidget(this);
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    if (mode == Mode::Make)
        m_list->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Checksum")});
    else
        m_list->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Result"), QStringLiteral("Expected")});
    m_list->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_list->header()->resizeSection(0, fontMetrics().averageCharWidth() * 36);
    layout->addWidget(m_list, 1);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1000);
    layout->addWidget(m_progress);
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto *buttons = new QDialogButtonBox(this);
    m_stop = buttons->addButton(QStringLiteral("&Stop"), QDialogButtonBox::ActionRole);
    connect(m_stop, &QPushButton::clicked, this, [this]() { *m_cancel = true; });
    m_copy = buttons->addButton(QStringLiteral("&Copy"), QDialogButtonBox::ActionRole);
    connect(m_copy, &QPushButton::clicked, this, [this]() {
        const QList<QTreeWidgetItem *> picked = m_list->selectedItems();
        if (picked.size() == 1 && m_mode == Mode::Make) {
            QApplication::clipboard()->setText(picked.first()->text(1));
            return;
        }
        QApplication::clipboard()->setText(resultText());
    });
    if (mode == Mode::Make) {
        m_save = buttons->addButton(QStringLiteral("&Save List..."), QDialogButtonBox::ActionRole);
        connect(m_save, &QPushButton::clicked, this, [this]() {
            const Checksum::Algo a = Checksum::Algo(m_algo->currentData().toInt());
            const QString path = QFileDialog::getSaveFileName(
                this, QStringLiteral("Save Checksum List"), defaultListPath(),
                QStringLiteral("%1 list (*.%2);;All files (*)").arg(Checksum::label(a), Checksum::extension(a)));
            if (path.isEmpty())
                return;
            QString error;
            if (!saveList(path, &error))
                QMessageBox::warning(this, windowTitle(), QStringLiteral("Could not save %1: %2").arg(path, error));
        });
    }
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(&m_watcher, &QFutureWatcher<void>::finished, this, &ChecksumDialog::onAllDone);
    resize(fontMetrics().averageCharWidth() * 110, fontMetrics().height() * 26);
}

ChecksumDialog::~ChecksumDialog()
{
    *m_cancel = true;
    m_watcher.waitForFinished();
}

void ChecksumDialog::setThen(Then then, const QString &saveTo)
{
    m_then = then;
    m_saveTo = saveTo;
}

bool ChecksumDialog::isRunning() const
{
    return m_watcher.isRunning();
}

void ChecksumDialog::collect()
{
    m_items.clear();
    m_listErrors.clear();
    m_total = 0;
    if (m_mode == Mode::Make) {
        const Checksum::Algo a = Checksum::Algo(m_algo->currentData().toInt());
        m_base = commonFolder(m_paths);
        for (const QString &p : std::as_const(m_paths)) {
            const QFileInfo fi(p);
            if (fi.isDir()) {
                QDirIterator it(p, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                QStringList found;
                while (it.hasNext())
                    found << it.next();
                std::sort(found.begin(), found.end());
                for (const QString &f : std::as_const(found)) {
                    const QFileInfo ff(f);
                    if (ff.isSymLink() && !ff.exists())
                        continue;
                    m_items << Item{f, relativeTo(f, m_base), {}, a, ff.size()};
                }
            } else if (fi.exists()) {
                m_items << Item{p, relativeTo(p, m_base), {}, a, fi.size()};
            }
        }
        std::stable_sort(m_items.begin(), m_items.end(), [](const Item &x, const Item &y) {
            return FileOps::naturalCompare(x.name, y.name) < 0;
        });
    } else {
        for (const QString &listPath : std::as_const(m_paths)) {
            QString error;
            const QList<Checksum::Entry> entries = Checksum::readList(listPath, &error);
            if (!error.isEmpty() || entries.isEmpty()) {
                m_listErrors << QStringLiteral("%1: %2").arg(Location::fileName(listPath),
                                                              error.isEmpty() ? QStringLiteral("no checksums found in it") : error);
                continue;
            }
            const QString dir = Location::parent(listPath);
            for (const Checksum::Entry &e : entries) {
                const QString path = QFileInfo(e.name).isAbsolute() ? e.name : Location::join(dir, e.name);
                const QFileInfo fi(path);
                m_items << Item{path, m_paths.size() > 1 ? Location::fileName(listPath) + QStringLiteral(": ") + e.name : e.name,
                                e.hash, e.algo, fi.exists() ? fi.size() : -1};
            }
        }
    }
    for (const Item &it : std::as_const(m_items))
        m_total += qMax<qint64>(0, it.size);
}

void ChecksumDialog::start()
{
    if (m_watcher.isRunning()) {
        *m_cancel = true;
        m_watcher.waitForFinished();
    }
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    collect();
    m_hashes = QList<QString>(m_items.size());
    m_ok = m_bad = m_missing = m_failed = 0;
    m_list->clear();
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (const Item &it : std::as_const(m_items)) {
        auto *row = new QTreeWidgetItem(m_list);
        row->setText(0, it.name);
        row->setToolTip(0, it.path);
        row->setFont(1, mono);
        if (m_mode == Mode::Verify) {
            row->setText(2, it.expected);
            row->setFont(2, mono);
            row->setFont(1, font());
        }
    }
    m_progress->setValue(0);
    m_progress->show();
    m_stop->setEnabled(true);
    if (m_copy)
        m_copy->setEnabled(false);
    if (m_save)
        m_save->setEnabled(false);
    if (m_algo)
        m_algo->setEnabled(true);
    updateSummary();
    run();
}

void ChecksumDialog::run()
{
    const QList<Item> items = m_items;
    const qint64 total = qMax<qint64>(1, m_total);
    auto cancel = m_cancel;
    QPointer<ChecksumDialog> self(this);
    m_watcher.setFuture(QtConcurrent::run([self, items, total, cancel]() {
        qint64 before = 0;
        qint64 lastShown = -1;
        for (int i = 0; i < items.size(); ++i) {
            if (*cancel)
                return;
            const Item &it = items.at(i);
            QString hash;
            QString error;
            if (it.size < 0 && !QFileInfo::exists(it.path)) {
                error = QStringLiteral("missing");
            } else {
                hash = Checksum::compute(it.path, it.algo, [&](qint64 done) {
                    const qint64 permille = (before + done) * 1000 / total;
                    if (permille != lastShown) {
                        lastShown = permille;
                        QMetaObject::invokeMethod(qApp, [self, permille, cancel]() {
                            if (self && !*cancel)
                                self->m_progress->setValue(int(permille));
                        }, Qt::QueuedConnection);
                    }
                    return !*cancel;
                }, &error);
                if (*cancel)
                    return;
            }
            before += qMax<qint64>(0, it.size);
            QMetaObject::invokeMethod(qApp, [self, i, hash, error, cancel]() {
                if (self && !*cancel)
                    self->onItemDone(i, hash, error);
            }, Qt::QueuedConnection);
        }
    }));
}

void ChecksumDialog::onItemDone(int index, const QString &hash, const QString &error)
{
    if (index < 0 || index >= m_items.size())
        return;
    m_hashes[index] = hash;
    QTreeWidgetItem *row = m_list->topLevelItem(index);
    const Item &it = m_items.at(index);
    if (m_mode == Mode::Make) {
        if (hash.isEmpty()) {
            ++m_failed;
            row->setText(1, QStringLiteral("(%1)").arg(error.isEmpty() ? QStringLiteral("cannot be read") : error));
            row->setForeground(1, QColor(0xc0, 0x30, 0x30));
        } else {
            row->setText(1, hash);
        }
    } else {
        Checksum::Result r;
        if (error == QLatin1String("missing"))
            r = Checksum::Result::Missing;
        else if (hash.isEmpty())
            r = Checksum::Result::Unreadable;
        else
            r = hash.compare(it.expected, Qt::CaseInsensitive) == 0 ? Checksum::Result::Ok : Checksum::Result::Bad;
        switch (r) {
        case Checksum::Result::Ok: ++m_ok; break;
        case Checksum::Result::Bad: ++m_bad; break;
        case Checksum::Result::Missing: ++m_missing; break;
        case Checksum::Result::Unreadable: ++m_failed; break;
        }
        row->setText(1, Checksum::resultText(r));
        row->setData(1, Qt::UserRole, int(r));
        row->setForeground(1, r == Checksum::Result::Ok ? QColor(0x2e, 0x9e, 0x4a) : QColor(0xd0, 0x3a, 0x3a));
        if (r == Checksum::Result::Bad)
            row->setToolTip(1, QStringLiteral("Its %1 is %2").arg(Checksum::label(it.algo), hash));
    }
    updateSummary();
}

void ChecksumDialog::onAllDone()
{
    const bool stopped = *m_cancel;
    m_progress->setValue(stopped ? m_progress->value() : 1000);
    m_stop->setEnabled(false);
    if (m_copy)
        m_copy->setEnabled(true);
    if (m_save)
        m_save->setEnabled(m_failed < m_items.size());
    updateSummary();
    if (stopped) {
        m_summary->setText(m_summary->text() + QStringLiteral(" Stopped."));
        Q_EMIT finished();
        return;
    }
    Q_EMIT finished();
    if (m_then == Then::Nothing || m_mode != Mode::Make)
        return;
    const Then then = std::exchange(m_then, Then::Nothing);
    if (m_failed > 0 || m_items.isEmpty()) {
        show(); // something to see
        return;
    }
    if (then == Then::Copy) {
        QApplication::clipboard()->setText(resultText());
    } else {
        QString error;
        const QString path = m_saveTo.isEmpty() ? defaultListPath() : m_saveTo;
        if (!saveList(path, &error)) {
            m_summary->setText(QStringLiteral("Could not save %1: %2").arg(path, error));
            show();
            return;
        }
    }
    close();
}

void ChecksumDialog::updateSummary()
{
    int done = 0;
    for (const QString &h : std::as_const(m_hashes))
        done += h.isEmpty() ? 0 : 1;
    QString text;
    if (m_mode == Mode::Make) {
        text = m_items.isEmpty() ? QStringLiteral("No files.")
                                 : QStringLiteral("%1 of %2 files, %3.").arg(done + m_failed).arg(m_items.size()).arg(FileOps::formatSize(m_total));
        if (m_failed)
            text += QStringLiteral(" %1 could not be read.").arg(m_failed);
    } else {
        QStringList parts;
        parts << QStringLiteral("%1 OK").arg(m_ok);
        if (m_bad)
            parts << QStringLiteral("%1 do not match").arg(m_bad);
        if (m_missing)
            parts << QStringLiteral("%1 missing").arg(m_missing);
        if (m_failed)
            parts << QStringLiteral("%1 cannot be read").arg(m_failed);
        const int checked = m_ok + m_bad + m_missing + m_failed;
        text = QStringLiteral("%1 of %2 checked: %3.").arg(checked).arg(m_items.size()).arg(parts.join(QStringLiteral(", ")));
        if (checked == m_items.size() && !m_items.isEmpty() && m_ok == checked)
            text = QStringLiteral("All %1 files are as they should be.").arg(checked);
    }
    if (!m_listErrors.isEmpty())
        text += QLatin1Char('\n') + m_listErrors.join(QLatin1Char('\n'));
    m_summary->setText(text);
}

QString ChecksumDialog::resultText() const
{
    if (m_mode == Mode::Verify)
        return m_summary->text();
    QList<Checksum::Entry> entries;
    for (int i = 0; i < m_items.size(); ++i) {
        if (!m_hashes.value(i).isEmpty())
            entries << Checksum::Entry{m_items.at(i).name, m_hashes.at(i), m_items.at(i).algo};
    }
    const Checksum::Algo a = m_algo ? Checksum::Algo(m_algo->currentData().toInt()) : Checksum::Algo::Md5;
    QString text = Checksum::format(entries, a);
    if (a == Checksum::Algo::Crc32)
        text = text.section(QLatin1Char('\n'), 1); // no comment line on the clipboard
    return text.trimmed();
}

QString ChecksumDialog::defaultListPath() const
{
    const Checksum::Algo a = m_algo ? Checksum::Algo(m_algo->currentData().toInt()) : Checksum::Algo::Md5;
    QString stem;
    if (m_paths.size() == 1)
        stem = Location::fileName(m_paths.first());
    else
        stem = Location::fileName(m_base);
    if (stem.isEmpty())
        stem = QStringLiteral("checksums");
    return Location::join(m_base, stem + QLatin1Char('.') + Checksum::extension(a));
}

bool ChecksumDialog::saveList(const QString &path, QString *error)
{
    QList<Checksum::Entry> entries;
    const QString dir = Location::parent(path);
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_hashes.value(i).isEmpty())
            continue;
        const QString p = m_items.at(i).path;
        const QString name = Location::isSameOrInside(p, dir) ? relativeTo(p, dir) : p;
        entries << Checksum::Entry{name, m_hashes.at(i), m_items.at(i).algo};
    }
    const Checksum::Algo a = Checksum::Algo(m_algo->currentData().toInt());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(Checksum::format(entries, a).toUtf8());
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    Q_EMIT listSaved(path);
    return true;
}
