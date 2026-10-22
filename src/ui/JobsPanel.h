#pragma once

#include <QHash>
#include <QWidget>

class JobManager;
class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

// Live list of file operations: progress, speed, time left, pause/resume,
// cancel, and "start now" for a job waiting in a device queue.
class JobsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit JobsPanel(JobManager &jobs, QWidget *parent = nullptr);

    int visibleJobCount() const { return m_rows.size(); }

Q_SIGNALS:
    // A job is still running after a moment: worth showing the panel.
    void attentionWanted();
    void becameEmpty();

private:
    struct Row
    {
        QWidget *widget = nullptr;
        QLabel *title = nullptr;
        QLabel *detail = nullptr;
        QProgressBar *bar = nullptr;
        QPushButton *pause = nullptr;
        QPushButton *cancel = nullptr;
        QPushButton *now = nullptr;
    };

    void addRow(int id);
    void updateRow(int id);
    void finishRow(int id);
    void removeRow(int id);

    JobManager &m_jobs;
    QVBoxLayout *m_list;
    QLabel *m_empty;
    QHash<int, Row> m_rows;
};
