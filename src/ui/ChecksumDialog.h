#pragma once

#include "core/Checksum.h"

#include <QDialog>
#include <QFutureWatcher>

#include <atomic>
#include <memory>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTreeWidget;

// Checksums of files (folders are walked), shown in a list to copy or save
// as an .md5/.sha1/.sha256/.sfv file; or checking files against such lists.
// The work runs in the background and can be stopped.
class ChecksumDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Make, Verify };
    enum class Then { Nothing, Copy, Save }; // done by itself when finished

    ChecksumDialog(Mode mode, const QStringList &paths, Checksum::Algo algo, QWidget *parent = nullptr);
    ~ChecksumDialog() override;

    // When finished: copy, or save to this file (empty: the usual name), and
    // close if nothing went wrong.
    void setThen(Then then, const QString &saveTo = {});
    void start();

    bool isRunning() const;
    QTreeWidget *list() const { return m_list; }
    QComboBox *algoBox() const { return m_algo; }
    // Make: "hash  name" lines; Verify: the counts.
    QString resultText() const;
    // The list file Make would write, and writing it.
    QString defaultListPath() const;
    bool saveList(const QString &path, QString *error);
    int okCount() const { return m_ok; }
    int badCount() const { return m_bad; }
    int missingCount() const { return m_missing; }

Q_SIGNALS:
    void finished();
    void listSaved(const QString &path);

private:
    struct Item
    {
        QString path; // what to read
        QString name; // as shown and written to a list
        QString expected;
        Checksum::Algo algo = Checksum::Algo::Md5;
        qint64 size = 0;
    };
    void collect();
    void run();
    void onItemDone(int index, const QString &hash, const QString &error);
    void onAllDone();
    void updateSummary();

    Mode m_mode;
    QStringList m_paths;
    QString m_base; // the folder names are relative to
    QList<Item> m_items;
    QList<QString> m_hashes;
    Then m_then = Then::Nothing;
    QString m_saveTo;
    QComboBox *m_algo = nullptr;
    QTreeWidget *m_list;
    QProgressBar *m_progress;
    QLabel *m_summary;
    QPushButton *m_copy = nullptr;
    QPushButton *m_save = nullptr;
    QPushButton *m_stop;
    QFutureWatcher<void> m_watcher;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    qint64 m_total = 0;
    int m_ok = 0, m_bad = 0, m_missing = 0, m_failed = 0;
    QStringList m_listErrors;
};
