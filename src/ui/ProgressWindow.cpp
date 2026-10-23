#include "ui/ProgressWindow.h"

#include "core/FileOps.h"
#include "core/JobManager.h"
#include "core/Paths.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kShowDelayMs = 400; // quick jobs never flash a window
constexpr int kCloseDelayMs = 700;

QString timeLeft(qint64 bytesLeft, double rate)
{
    if (rate < 1.0 || bytesLeft <= 0)
        return {};
    const qint64 secs = static_cast<qint64>(static_cast<double>(bytesLeft) / rate);
    if (secs < 60)
        return QStringLiteral("%1 s left").arg(secs);
    if (secs < 3600)
        return QStringLiteral("%1 min %2 s left").arg(secs / 60).arg(secs % 60);
    return QStringLiteral("%1 h %2 min left").arg(secs / 3600).arg((secs % 3600) / 60);
}

QString verb(JobType t)
{
    switch (t) {
    case JobType::Copy:
        return QStringLiteral("Copying");
    case JobType::Move:
        return QStringLiteral("Moving");
    case JobType::Trash:
        return QStringLiteral("Trashing");
    case JobType::Delete:
        return QStringLiteral("Deleting");
    case JobType::Pack:
        return QStringLiteral("Packing");
    case JobType::Unpack:
        return QStringLiteral("Extracting");
    case JobType::Split:
        return QStringLiteral("Splitting");
    case JobType::Join:
        return QStringLiteral("Joining");
    }
    return {};
}

QString stateText(JobState s)
{
    switch (s) {
    case JobState::Queued:
        return QStringLiteral("Waiting for the job ahead on this drive to finish.");
    case JobState::Scanning:
        return QStringLiteral("Counting files...");
    case JobState::Paused:
        return QStringLiteral("Paused.");
    case JobState::WaitingForUser:
        return QStringLiteral("Waiting for your answer.");
    case JobState::Finished:
        return QStringLiteral("Done.");
    case JobState::Failed:
        return QStringLiteral("Finished, but some items could not be processed:");
    case JobState::Canceled:
        return QStringLiteral("Canceled.");
    case JobState::Running:
        return {};
    }
    return {};
}

void setElided(QLabel *label, const QString &text)
{
    label->setToolTip(text);
    const int width = qMax(200, label->width());
    label->setText(QFontMetrics(label->font()).elidedText(text, Qt::ElideMiddle, width));
}

int percentOf(qint64 done, qint64 total)
{
    if (total <= 0)
        return 0;
    return static_cast<int>(1000.0 * static_cast<double>(done) / static_cast<double>(total));
}

} // namespace

// ---------------------------------------------------------------- window

bool ProgressWindow::closeWhenDoneDefault()
{
    QSettings s(Paths::configFile(QStringLiteral("linopus.ini")), QSettings::IniFormat);
    return s.value(QStringLiteral("Jobs/closeWhenDone"), true).toBool();
}

void ProgressWindow::setCloseWhenDoneDefault(bool on)
{
    QSettings s(Paths::configFile(QStringLiteral("linopus.ini")), QSettings::IniFormat);
    s.setValue(QStringLiteral("Jobs/closeWhenDone"), on);
}

ProgressWindow::ProgressWindow(JobManager &jobs, int jobId, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_jobs(jobs)
    , m_id(jobId)
    , m_title(new QLabel(this))
    , m_from(new QLabel(this))
    , m_to(new QLabel(this))
    , m_file(new QLabel(this))
    , m_fileBar(new QProgressBar(this))
    , m_totalBar(new QProgressBar(this))
    , m_detail(new QLabel(this))
    , m_state(new QLabel(this))
    , m_errors(new QListWidget(this))
    , m_autoClose(new QCheckBox(QStringLiteral("Close when finished"), this))
    , m_now(new QPushButton(QStringLiteral("Start Now"), this))
    , m_pause(new QPushButton(QStringLiteral("Pause"), this))
    , m_cancel(new QPushButton(QStringLiteral("Cancel"), this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(QStringLiteral("progress-window"));
    setMinimumWidth(460);

    QFont big = m_title->font();
    big.setBold(true);
    big.setPointSizeF(big.pointSizeF() * 1.15);
    m_title->setFont(big);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_title);

    auto *where = new QGridLayout;
    where->addWidget(new QLabel(QStringLiteral("From:"), this), 0, 0);
    where->addWidget(m_from, 0, 1);
    where->addWidget(new QLabel(QStringLiteral("To:"), this), 1, 0);
    where->addWidget(m_to, 1, 1);
    where->setColumnStretch(1, 1);
    layout->addLayout(where);

    layout->addSpacing(4);
    layout->addWidget(m_file);
    m_fileBar->setTextVisible(false);
    m_fileBar->setMaximumHeight(8);
    m_fileBar->setRange(0, 1000);
    layout->addWidget(m_fileBar);
    m_totalBar->setRange(0, 1000);
    m_totalBar->setFormat(QStringLiteral("%p%"));
    layout->addWidget(m_totalBar);
    layout->addWidget(m_detail);
    m_state->setWordWrap(true);
    layout->addWidget(m_state);
    m_errors->setVisible(false);
    m_errors->setMinimumHeight(90);
    layout->addWidget(m_errors);

    auto *buttons = new QHBoxLayout;
    m_autoClose->setChecked(closeWhenDoneDefault());
    buttons->addWidget(m_autoClose);
    buttons->addStretch(1);
    buttons->addWidget(m_now);
    buttons->addWidget(m_pause);
    buttons->addWidget(m_cancel);
    layout->addLayout(buttons);

    m_now->setToolTip(QStringLiteral("Run alongside the job ahead instead of waiting"));
    connect(m_now, &QPushButton::clicked, this, [this]() { m_jobs.startNow(m_id); });
    connect(m_pause, &QPushButton::clicked, this, [this]() {
        if (m_jobs.snapshot(m_id).state == JobState::Paused)
            m_jobs.resume(m_id);
        else
            m_jobs.pause(m_id);
        refresh();
    });
    connect(m_cancel, &QPushButton::clicked, this, [this]() {
        if (m_done)
            close();
        else
            m_jobs.cancel(m_id);
    });
    connect(m_autoClose, &QCheckBox::toggled, this, [](bool on) { setCloseWhenDoneDefault(on); });

    refresh();
}

void ProgressWindow::refresh()
{
    if (!m_jobs.hasJob(m_id))
        return;
    const JobSnapshot s = m_jobs.snapshot(m_id);
    const bool bytes = s.totalBytes > 0;
    const bool copying = s.type == JobType::Copy || s.type == JobType::Move || s.type == JobType::Pack || s.type == JobType::Unpack
        || s.type == JobType::Split || s.type == JobType::Join;

    m_title->setText(s.title);
    setElided(m_from, s.fromFolder);
    setElided(m_to, copying ? s.toFolder : (s.type == JobType::Trash ? QStringLiteral("Trash") : QStringLiteral("(gone for good)")));

    if (!s.currentItem.isEmpty() && !s.isDone())
        setElided(m_file, QFileInfo(s.currentItem).fileName());
    else
        m_file->clear();
    m_fileBar->setVisible(copying && s.currentSize > 0 && !s.isDone());
    m_fileBar->setValue(percentOf(s.currentDone, s.currentSize));

    if (s.state == JobState::Scanning) {
        m_totalBar->setRange(0, 0);
    } else {
        m_totalBar->setRange(0, 1000);
        if (s.isDone() && s.state != JobState::Canceled)
            m_totalBar->setValue(1000);
        else if (bytes)
            m_totalBar->setValue(percentOf(s.doneBytes, s.totalBytes));
        else
            m_totalBar->setValue(percentOf(s.doneItems, s.totalItems));
    }

    QStringList parts;
    if (bytes)
        parts << QStringLiteral("%1 of %2").arg(FileOps::formatSize(s.doneBytes), FileOps::formatSize(s.totalBytes));
    if (s.totalItems > 0)
        parts << QStringLiteral("%1 of %2 %3").arg(s.doneItems).arg(s.totalItems).arg(s.totalItems == 1 ? QStringLiteral("item") : QStringLiteral("items"));
    if (s.state == JobState::Running && s.bytesPerSecond > 0 && bytes) {
        parts << QStringLiteral("%1/s").arg(FileOps::formatSize(static_cast<qint64>(s.bytesPerSecond)));
        const QString left = timeLeft(s.totalBytes - s.doneBytes, s.bytesPerSecond);
        if (!left.isEmpty())
            parts << left;
    }
    m_detail->setText(parts.join(QStringLiteral("  -  ")));

    m_state->setText(stateText(s.state));
    m_state->setVisible(!m_state->text().isEmpty());

    if (m_errors->count() != s.errors.size()) {
        m_errors->clear();
        m_errors->addItems(s.errors);
    }
    m_errors->setVisible(!s.errors.isEmpty());

    // Percent in the title so it reads from the taskbar.
    const int pct = m_totalBar->maximum() > 0 ? m_totalBar->value() / 10 : 0;
    setWindowTitle(s.isDone() ? QStringLiteral("%1 - Linopus").arg(stateText(s.state).chopped(1))
                              : QStringLiteral("%1% %2 - Linopus").arg(pct).arg(verb(s.type)));

    m_now->setVisible(s.state == JobState::Queued);
    m_pause->setVisible(!s.isDone());
    m_pause->setText(s.state == JobState::Paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
    m_cancel->setText(s.isDone() ? QStringLiteral("Close") : QStringLiteral("Cancel"));
    m_autoClose->setVisible(!s.isDone());
}

void ProgressWindow::jobFinished()
{
    m_done = true;
    refresh();
    const JobSnapshot s = m_jobs.snapshot(m_id);
    if (s.state == JobState::Failed) {
        show();
        raise();
        return; // stays until read
    }
    if (s.state == JobState::Canceled || m_autoClose->isChecked())
        QTimer::singleShot(s.state == JobState::Canceled ? 0 : kCloseDelayMs, this, &QWidget::close);
}

void ProgressWindow::closeEvent(QCloseEvent *event)
{
    if (!m_done) {
        // Closing does not stop the copy; Window > File Operations brings it back.
        hide();
        event->ignore();
        return;
    }
    Q_EMIT dismissed(m_id);
    event->accept();
}

// ---------------------------------------------------------------- manager

ProgressWindows::ProgressWindows(JobManager &jobs, QObject *parent)
    : QObject(parent)
    , m_jobs(jobs)
{
    connect(&m_jobs, &JobManager::jobAdded, this, [this](int id) {
        QTimer::singleShot(kShowDelayMs, this, [this, id]() {
            if (m_jobs.hasJob(id) && !m_jobs.snapshot(id).isDone())
                open(id);
        });
    });
    connect(&m_jobs, &JobManager::jobChanged, this, [this](int id) {
        if (ProgressWindow *w = windowFor(id))
            w->refresh();
    });
    connect(&m_jobs, &JobManager::jobFinished, this, [this](int id) {
        ProgressWindow *w = windowFor(id);
        const JobSnapshot s = m_jobs.snapshot(id);
        if (!w && s.state == JobState::Failed) {
            open(id); // finished too fast for a window, but there is something to read
            w = windowFor(id);
        }
        if (w)
            w->jobFinished();
        else
            m_jobs.remove(id);
    });
}

ProgressWindows::~ProgressWindows()
{
    for (const auto &w : std::as_const(m_windows)) {
        if (w)
            w->deleteLater();
    }
}

ProgressWindow *ProgressWindows::windowFor(int jobId) const
{
    return m_windows.value(jobId).data();
}

void ProgressWindows::open(int id)
{
    if (windowFor(id))
        return;
    auto *w = new ProgressWindow(m_jobs, id);
    m_windows.insert(id, w);
    connect(w, &ProgressWindow::dismissed, this, [this](int jobId) {
        m_windows.remove(jobId);
        if (m_jobs.hasJob(jobId) && m_jobs.snapshot(jobId).isDone())
            m_jobs.remove(jobId);
        if (m_windows.isEmpty())
            m_cascade = 0;
    });
    w->adjustSize();
    place(w);
    w->show();
    w->raise();
}

void ProgressWindows::place(ProgressWindow *w)
{
    // Centered over the lister that started it, each new one stepped down so
    // several jobs are all visible.
    QRect area;
    if (m_anchor)
        area = m_anchor->frameGeometry();
    else if (QScreen *screen = w->screen())
        area = screen->availableGeometry();
    if (!area.isValid())
        return;
    const QSize size = w->sizeHint().expandedTo(w->minimumSize());
    QPoint topLeft = area.center() - QPoint(size.width() / 2, size.height() / 2);
    topLeft += QPoint(28, 28) * (m_cascade++ % 6);
    w->move(topLeft);
}

void ProgressWindows::showAll()
{
    for (const auto &w : std::as_const(m_windows)) {
        if (w) {
            w->show();
            w->raise();
        }
    }
}
