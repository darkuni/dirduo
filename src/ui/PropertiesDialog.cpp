#include "ui/PropertiesDialog.h"

#include "core/FileOps.h"
#include "core/Location.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QDirIterator>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent>

PropertiesDialog::PropertiesDialog(const QStringList &paths, QWidget *parent)
    : QDialog(parent)
    , m_paths(paths)
    , m_stop(std::make_shared<std::atomic<bool>>(false))
{
    for (const QString &p : paths)
        m_infos << Attributes::read(p);
    const bool one = paths.size() == 1;
    const Attributes::Info &first = m_infos.first();
    bool anyDir = false;
    for (const Attributes::Info &i : std::as_const(m_infos))
        anyDir = anyDir || (i.isDir && !i.isLink);
    setWindowTitle(one ? QStringLiteral("Properties - %1").arg(Location::fileName(paths.first()))
                       : QStringLiteral("Properties - %1 items").arg(paths.size()));

    auto *layout = new QVBoxLayout(this);

    // What it is.
    auto *head = new QHBoxLayout;
    auto *icon = new QLabel(this);
    QFileIconProvider icons;
    const int px = fontMetrics().height() * 3;
    icon->setPixmap(icons.icon(QFileInfo(paths.first())).pixmap(px, px));
    head->addWidget(icon);
    auto *title = new QLabel(one ? Location::fileName(paths.first()) : QStringLiteral("%1 items").arg(paths.size()), this);
    QFont big = title->font();
    big.setPointSizeF(big.pointSizeF() * 1.25);
    big.setBold(true);
    title->setFont(big);
    title->setWordWrap(true);
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    head->addWidget(title, 1);
    layout->addLayout(head);

    auto *form = new QFormLayout;
    auto *where = new QLabel(Location::display(Location::parent(paths.first())), this);
    where->setWordWrap(true);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("Location:"), where);
    if (one) {
        QString kind;
        if (first.isLink)
            kind = first.brokenLink ? QStringLiteral("Link (broken)") : QStringLiteral("Link to a %1").arg(first.isDir ? QStringLiteral("folder") : QStringLiteral("file"));
        else if (first.isDir)
            kind = QStringLiteral("Folder");
        else
            kind = QMimeDatabase().mimeTypeForFile(paths.first()).comment();
        form->addRow(QStringLiteral("Type:"), new QLabel(kind, this));
    }
    m_size = new QLabel(this);
    m_size->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("Size:"), m_size);
    if (one && first.isLink) {
        m_link = new QLabel(first.linkTarget, this);
        m_link->setWordWrap(true);
        m_link->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(QStringLiteral("Points to:"), m_link);
    }
    if (one)
        form->addRow(QStringLiteral("Owner:"), new QLabel(QStringLiteral("%1 (group %2)").arg(first.owner, first.group), this));
    layout->addLayout(form);

    // Dates.
    auto *dates = new QGroupBox(QStringLiteral("Dates"), this);
    auto *df = new QFormLayout(dates);
    auto dateEdit = [this](const QDateTime &t, bool *touched) {
        auto *e = new QDateTimeEdit(t, this);
        e->setCalendarPopup(true);
        e->setDisplayFormat(QLocale().dateTimeFormat(QLocale::ShortFormat).contains(QLatin1String("ss"))
                                ? QLocale().dateTimeFormat(QLocale::ShortFormat)
                                : QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        connect(e, &QDateTimeEdit::dateTimeChanged, this, [touched]() { *touched = true; });
        return e;
    };
    m_modified = dateEdit(first.modified, &m_modifiedTouched);
    m_accessed = dateEdit(first.accessed, &m_accessedTouched);
    auto *nowRow = new QHBoxLayout;
    nowRow->addWidget(m_modified, 1);
    auto *now = new QPushButton(QStringLiteral("Now"), this);
    connect(now, &QPushButton::clicked, this, [this]() { m_modified->setDateTime(QDateTime::currentDateTime()); });
    nowRow->addWidget(now);
    df->addRow(QStringLiteral("&Modified:"), nowRow);
    df->addRow(QStringLiteral("&Accessed:"), m_accessed);
    if (one && first.created.isValid())
        df->addRow(QStringLiteral("Created:"), new QLabel(QLocale().toString(first.created, QLocale::ShortFormat), this));
    if (!one) {
        auto *note = new QLabel(QStringLiteral("Showing the first item's dates. A date you change is set on all of them."), this);
        note->setWordWrap(true);
        df->addRow(note);
    }
    layout->addWidget(dates);

    // Permissions: tri-state when the items differ.
    auto *perms = new QGroupBox(QStringLiteral("Permissions"), this);
    auto *grid = new QGridLayout(perms);
    const QString whoNames[3] = {QStringLiteral("Owner"), QStringLiteral("Group"), QStringLiteral("Others")};
    const QString whatNames[3] = {QStringLiteral("Read"), QStringLiteral("Write"),
                                  anyDir && one ? QStringLiteral("Open") : QStringLiteral("Run")};
    for (int what = 0; what < 3; ++what)
        grid->addWidget(new QLabel(whatNames[what], perms), 0, what + 1, Qt::AlignCenter);
    for (int who = 0; who < 3; ++who) {
        grid->addWidget(new QLabel(whoNames[who], perms), who + 1, 0);
        for (int what = 0; what < 3; ++what) {
            const int bit = (4 >> what) << ((2 - who) * 3);
            int on = 0;
            int links = 0;
            for (const Attributes::Info &i : std::as_const(m_infos)) {
                if (i.isLink && i.brokenLink) {
                    ++links;
                    continue;
                }
                if (i.mode & bit)
                    ++on;
            }
            const int counted = int(m_infos.size()) - links;
            auto *box = new QCheckBox(perms);
            Qt::CheckState state = on == 0 ? Qt::Unchecked : (on == counted ? Qt::Checked : Qt::PartiallyChecked);
            if (state == Qt::PartiallyChecked)
                box->setTristate(true);
            box->setCheckState(state);
            connect(box, &QCheckBox::stateChanged, box, [box](int s) {
                // Once clicked, a mixed box becomes a plain on/off one.
                if (s != Qt::PartiallyChecked)
                    box->setTristate(false);
            });
            m_initial[who][what] = state;
            m_perm[who][what] = box;
            grid->addWidget(box, who + 1, what + 1, Qt::AlignCenter);
        }
    }
    if (one && !(first.isLink && first.brokenLink)) {
        auto *mode = new QLabel(QStringLiteral("%1   (%2)").arg(Attributes::modeString(first.mode, first.isDir), Attributes::octal(first.mode)), perms);
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mode->setFont(mono);
        grid->addWidget(mode, 4, 0, 1, 4);
    }
    layout->addWidget(perms);

    m_recurse = new QCheckBox(QStringLiteral("Change everything inside the folders too"), this);
    m_recurse->setVisible(anyDir);
    layout->addWidget(m_recurse);

    // Only local items can be changed.
    bool local = true;
    for (const QString &p : paths)
        local = local && Location::isLocal(p);
    dates->setEnabled(local);
    perms->setEnabled(local);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Size: files at once, folders counted off the main thread.
    qint64 bytes = 0;
    int files = 0;
    QStringList folders;
    for (int k = 0; k < m_infos.size(); ++k) {
        const Attributes::Info &i = m_infos.at(k);
        if (i.isDir && !i.isLink)
            folders << paths.at(k);
        else if (!i.isDir) {
            bytes += i.size;
            ++files;
        }
    }
    auto show = [this](const Totals &t, bool done) {
        QString text = FileOps::formatSize(t.bytes);
        if (t.bytes >= 1024)
            text += QStringLiteral(" (%1 bytes)").arg(QLocale().toString(t.bytes));
        if (t.folders > 0 || t.files > 1)
            text += QStringLiteral(" - %1 %2, %3 %4")
                        .arg(t.files)
                        .arg(t.files == 1 ? QStringLiteral("file") : QStringLiteral("files"))
                        .arg(t.folders)
                        .arg(t.folders == 1 ? QStringLiteral("folder") : QStringLiteral("folders"));
        if (!done)
            text += QStringLiteral(" - counting...");
        m_size->setText(text);
    };
    Totals start;
    start.bytes = bytes;
    start.files = files;
    if (folders.isEmpty()) {
        show(start, true);
    } else {
        show(start, false);
        auto stop = m_stop;
        connect(&m_watcher, &QFutureWatcher<Totals>::finished, this, [this, show]() { show(m_watcher.result(), true); });
        m_watcher.setFuture(QtConcurrent::run([folders, start, stop]() {
            Totals t = start;
            for (const QString &f : folders) {
                ++t.folders;
                QDirIterator it(f, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                QDirIterator::Subdirectories);
                while (it.hasNext() && !*stop) {
                    it.next();
                    const QFileInfo fi = it.fileInfo();
                    if (fi.isSymLink())
                        ++t.files;
                    else if (fi.isDir())
                        ++t.folders;
                    else {
                        ++t.files;
                        t.bytes += fi.size();
                    }
                }
            }
            return t;
        }));
    }
    setMinimumWidth(fontMetrics().averageCharWidth() * 56);
}

PropertiesDialog::~PropertiesDialog()
{
    *m_stop = true;
    m_watcher.waitForFinished();
}

QString PropertiesDialog::sizeText() const
{
    return m_size->text();
}

QString PropertiesDialog::linkText() const
{
    return m_link ? m_link->text() : QString();
}

Attributes::Change PropertiesDialog::change() const
{
    Attributes::Change c;
    if (m_modifiedTouched)
        c.modified = m_modified->dateTime();
    if (m_accessedTouched)
        c.accessed = m_accessed->dateTime();
    for (int who = 0; who < 3; ++who) {
        for (int what = 0; what < 3; ++what) {
            const Qt::CheckState now = m_perm[who][what]->checkState();
            if (now == m_initial[who][what] || now == Qt::PartiallyChecked)
                continue;
            const int bit = (4 >> what) << ((2 - who) * 3);
            if (now == Qt::Checked)
                c.mode.add |= bit;
            else
                c.mode.remove |= bit;
        }
    }
    c.recurse = m_recurse->isVisible() && m_recurse->isChecked();
    return c;
}

bool PropertiesDialog::applyChanges(QStringList *problems)
{
    const Attributes::Change c = change();
    if (c.isEmpty())
        return true;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QStringList errors;
    Attributes::apply(m_paths, c, &errors);
    QApplication::restoreOverrideCursor();
    Q_EMIT changed();
    if (problems)
        *problems = errors;
    return errors.isEmpty();
}

void PropertiesDialog::accept()
{
    QStringList problems;
    if (!applyChanges(&problems)) {
        QString text = problems.mid(0, 10).join(QLatin1Char('\n'));
        if (problems.size() > 10)
            text += QStringLiteral("\n...and %1 more.").arg(problems.size() - 10);
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Some items could not be changed:\n\n%1").arg(text));
    }
    QDialog::accept();
}
