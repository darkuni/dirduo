#pragma once

#include <QHash>
#include <QPointer>
#include <QWidget>

class JobManager;
class QCheckBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;

// One window per file operation, Opus style. Every job runs on its own thread,
// so several windows can move along at once while the listers stay usable.
class ProgressWindow : public QWidget
{
    Q_OBJECT
public:
    ProgressWindow(JobManager &jobs, int jobId, QWidget *parent = nullptr);

    int jobId() const { return m_id; }
    void refresh();
    void jobFinished();

    static bool closeWhenDoneDefault();
    static void setCloseWhenDoneDefault(bool on);

Q_SIGNALS:
    void dismissed(int jobId);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    JobManager &m_jobs;
    const int m_id;
    bool m_done = false;

    QLabel *m_title;
    QLabel *m_from;
    QLabel *m_to;
    QLabel *m_file;
    QProgressBar *m_fileBar;
    QProgressBar *m_totalBar;
    QLabel *m_detail;
    QLabel *m_state;
    QListWidget *m_errors;
    QCheckBox *m_autoClose;
    QPushButton *m_now;
    QPushButton *m_pause;
    QPushButton *m_cancel;
};

// Opens a window for each job that lasts long enough to matter, closes it when
// the job ends cleanly, and keeps it open when something went wrong.
class ProgressWindows : public QObject
{
    Q_OBJECT
public:
    explicit ProgressWindows(JobManager &jobs, QObject *parent = nullptr);
    ~ProgressWindows() override;

    ProgressWindow *windowFor(int jobId) const;
    void showAll();   // bring every open window forward
    int count() const { return m_windows.size(); }

    // Placed over this window when a job's window opens.
    void setAnchor(QWidget *anchor) { m_anchor = anchor; }

private:
    void open(int id);
    void place(ProgressWindow *w);

    JobManager &m_jobs;
    QHash<int, QPointer<ProgressWindow>> m_windows;
    QPointer<QWidget> m_anchor;
    int m_cascade = 0;
};
