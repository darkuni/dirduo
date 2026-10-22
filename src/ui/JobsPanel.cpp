#include "ui/JobsPanel.h"

#include "core/FileOps.h"
#include "core/JobManager.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace {

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

QString stateWord(JobState s)
{
    switch (s) {
    case JobState::Queued:
        return QStringLiteral("Waiting for the job ahead on this drive");
    case JobState::Scanning:
        return QStringLiteral("Counting files...");
    case JobState::Running:
        return {};
    case JobState::Paused:
        return QStringLiteral("Paused");
    case JobState::WaitingForUser:
        return QStringLiteral("Waiting for your answer");
    case JobState::Finished:
        return QStringLiteral("Done");
    case JobState::Failed:
        return QStringLiteral("Finished with problems");
    case JobState::Cancelled:
        return QStringLiteral("Cancelled");
    }
    return {};
}

} // namespace

JobsPanel::JobsPanel(JobManager &jobs, QWidget *parent)
    : QWidget(parent)
    , m_jobs(jobs)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget(scroll);
    m_list = new QVBoxLayout(inner);
    m_list->setContentsMargins(6, 6, 6, 6);
    m_empty = new QLabel(QStringLiteral("No file operations running."), inner);
    m_empty->setAlignment(Qt::AlignCenter);
    m_list->addWidget(m_empty);
    m_list->addStretch(1);
    scroll->setWidget(inner);
    outer->addWidget(scroll);

    connect(&m_jobs, &JobManager::jobAdded, this, &JobsPanel::addRow);
    connect(&m_jobs, &JobManager::jobChanged, this, &JobsPanel::updateRow);
    connect(&m_jobs, &JobManager::jobFinished, this, &JobsPanel::finishRow);
    connect(&m_jobs, &JobManager::jobRemoved, this, &JobsPanel::removeRow);
    for (int id : m_jobs.jobIds()) {
        if (!m_jobs.snapshot(id).isDone())
            addRow(id);
    }
}

void JobsPanel::addRow(int id)
{
    if (m_rows.contains(id))
        return;
    Row r;
    r.widget = new QWidget(this);
    auto *v = new QVBoxLayout(r.widget);
    v->setContentsMargins(0, 4, 0, 4);
    v->setSpacing(2);
    auto *top = new QHBoxLayout;
    r.title = new QLabel(r.widget);
    QFont bold = r.title->font();
    bold.setBold(true);
    r.title->setFont(bold);
    top->addWidget(r.title, 1);
    r.now = new QPushButton(QStringLiteral("Start Now"), r.widget);
    r.now->setToolTip(QStringLiteral("Run alongside the job ahead instead of waiting"));
    r.pause = new QPushButton(QStringLiteral("Pause"), r.widget);
    r.cancel = new QPushButton(QStringLiteral("Cancel"), r.widget);
    top->addWidget(r.now);
    top->addWidget(r.pause);
    top->addWidget(r.cancel);
    v->addLayout(top);
    r.bar = new QProgressBar(r.widget);
    r.bar->setTextVisible(false);
    r.bar->setMaximumHeight(10);
    v->addWidget(r.bar);
    r.detail = new QLabel(r.widget);
    r.detail->setWordWrap(true);
    v->addWidget(r.detail);

    connect(r.pause, &QPushButton::clicked, this, [this, id]() {
        const JobSnapshot s = m_jobs.snapshot(id);
        if (s.state == JobState::Paused)
            m_jobs.resume(id);
        else
            m_jobs.pause(id);
    });
    connect(r.cancel, &QPushButton::clicked, this, [this, id]() {
        if (m_jobs.snapshot(id).isDone())
            removeRow(id);
        else
            m_jobs.cancel(id);
    });
    connect(r.now, &QPushButton::clicked, this, [this, id]() { m_jobs.startNow(id); });

    m_list->insertWidget(m_list->count() - 1, r.widget); // oldest first, above the stretch
    m_rows.insert(id, r);
    m_empty->hide();
    updateRow(id);

    // Quick jobs come and go without popping anything up.
    QTimer::singleShot(700, this, [this, id]() {
        if (m_rows.contains(id) && !m_jobs.snapshot(id).isDone())
            Q_EMIT attentionWanted();
    });
}

void JobsPanel::updateRow(int id)
{
    auto it = m_rows.find(id);
    if (it == m_rows.end())
        return;
    if (!m_jobs.hasJob(id)) {
        removeRow(id);
        return;
    }
    const JobSnapshot s = m_jobs.snapshot(id);
    Row &r = *it;
    r.title->setText(s.title);

    if (s.totalBytes > 0) {
        r.bar->setRange(0, 1000);
        r.bar->setValue(static_cast<int>(1000.0 * static_cast<double>(s.doneBytes) / static_cast<double>(s.totalBytes)));
    } else if (s.totalItems > 0) {
        r.bar->setRange(0, s.totalItems);
        r.bar->setValue(s.doneItems);
    } else if (s.state == JobState::Scanning) {
        r.bar->setRange(0, 0); // busy indicator while counting
    } else {
        r.bar->setRange(0, 1);
        r.bar->setValue(s.isDone() ? 1 : 0);
    }

    QStringList parts;
    const QString word = stateWord(s.state);
    if (!word.isEmpty())
        parts << word;
    if (s.state == JobState::Running || s.state == JobState::Paused) {
        if (s.totalBytes > 0)
            parts << QStringLiteral("%1 of %2").arg(FileOps::formatSize(s.doneBytes), FileOps::formatSize(s.totalBytes));
        if (s.totalItems > 0)
            parts << QStringLiteral("%1 of %2 items").arg(s.doneItems).arg(s.totalItems);
        if (s.state == JobState::Running && s.bytesPerSecond > 0) {
            parts << QStringLiteral("%1/s").arg(FileOps::formatSize(static_cast<qint64>(s.bytesPerSecond)));
            const QString left = timeLeft(s.totalBytes - s.doneBytes, s.bytesPerSecond);
            if (!left.isEmpty())
                parts << left;
        }
    }
    if (!s.currentItem.isEmpty() && !s.isDone())
        parts << QFileInfo(s.currentItem).fileName();
    if (!s.errors.isEmpty())
        parts << QStringLiteral("%1 problem%2").arg(s.errors.size()).arg(s.errors.size() == 1 ? QString() : QStringLiteral("s"));
    r.detail->setText(parts.join(QStringLiteral(" - ")));
    r.detail->setToolTip(s.errors.join(QLatin1Char('\n')));

    r.pause->setText(s.state == JobState::Paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
    r.pause->setVisible(!s.isDone());
    r.now->setVisible(s.state == JobState::Queued);
    r.cancel->setText(s.isDone() ? QStringLiteral("Dismiss") : QStringLiteral("Cancel"));
}

void JobsPanel::finishRow(int id)
{
    updateRow(id);
    const JobSnapshot s = m_jobs.snapshot(id);
    if (s.state == JobState::Failed) {
        // Problems stay listed until dismissed; the tooltip lists them.
        if (m_rows.contains(id))
            m_rows[id].detail->setText(QStringLiteral("Finished with %1 problem%2 (hover for details): %3")
                                           .arg(s.errors.size())
                                           .arg(s.errors.size() == 1 ? QString() : QStringLiteral("s"))
                                           .arg(s.errors.value(0)));
        Q_EMIT attentionWanted();
        return;
    }
    QTimer::singleShot(s.state == JobState::Cancelled ? 1500 : 3000, this, [this, id]() { removeRow(id); });
}

void JobsPanel::removeRow(int id)
{
    auto it = m_rows.find(id);
    if (it == m_rows.end())
        return;
    it->widget->deleteLater();
    m_rows.erase(it);
    // Removing from the manager tells every other lister's panel too.
    if (m_jobs.hasJob(id) && m_jobs.snapshot(id).isDone())
        m_jobs.remove(id);
    if (m_rows.isEmpty()) {
        m_empty->show();
        Q_EMIT becameEmpty();
    }
}
